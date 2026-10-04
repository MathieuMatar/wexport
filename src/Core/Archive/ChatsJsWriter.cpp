#include "ChatsJsWriter.h"

#include <cstring>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "../Util/FileSystem.h"
#include "../Util/Strings.h"

namespace ck {

namespace fs = std::filesystem;

namespace {

constexpr const char* kMissing = "The media is missing";
constexpr const char* kCallLogJid = "000000000000000";

// Mirrors the viewer's classification in index.html convert().
std::string Extension(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return ToLowerAscii(path.substr(dot + 1));
}

class StatsSax : public nlohmann::json_sax<nlohmann::json> {
public:
    StatsSax(ArchiveStats& stats, const CancelToken& cancel) : s_(stats), cancel_(cancel) {}

    bool null() override { Value(Kind::Null); return true; }
    bool boolean(bool v) override { Value(v ? Kind::True : Kind::False); return true; }
    bool number_integer(number_integer_t) override { Value(Kind::Other); return true; }
    bool number_unsigned(number_unsigned_t) override { Value(Kind::Other); return true; }
    bool number_float(number_float_t, const string_t&) override { Value(Kind::Other); return true; }
    bool binary(binary_t&) override { Value(Kind::Other); return true; }
    bool string(string_t& v) override {
        if (depth_ == 4 && inMessage_) {
            if (key_ == "data") data_ = v, hasData_ = true;
            else if (key_ == "mime") mime_ = v;
        }
        Value(Kind::Other);
        return true;
    }
    bool start_object(std::size_t) override {
        ++depth_;
        if (depth_ == 2) {
            chatJid_ = key_;
            chatMessages_ = 0;
        } else if (depth_ == 3) {
            inMessages_ = key_ == "messages";
        } else if (depth_ == 4 && inMessages_) {
            inMessage_ = true;
            media_ = meta_ = sticker_ = hasData_ = false;
            data_.clear();
            mime_.clear();
        }
        if ((++objects_ & 0xFFF) == 0 && cancel_.IsCancelled()) return false;
        return true;
    }
    bool end_object() override {
        if (depth_ == 4 && inMessage_) {
            EndMessage();
            inMessage_ = false;
        } else if (depth_ == 3) {
            inMessages_ = false;
        } else if (depth_ == 2) {
            if (chatJid_ != kCallLogJid && chatMessages_ > 0) {
                ++s_.chats;
                if (EndsWith(chatJid_, "@g.us")) ++s_.groups;
            }
        }
        --depth_;
        return true;
    }
    bool start_array(std::size_t) override { ++depth_; return true; }
    bool end_array() override { --depth_; return true; }
    bool key(string_t& k) override {
        key_ = k;
        return true;
    }
    bool parse_error(std::size_t position, const std::string&, const nlohmann::detail::exception& ex) override {
        error = "Invalid JSON at byte " + std::to_string(position) + ": " + ex.what();
        return false;
    }

    std::string error;

private:
    enum class Kind { Null, True, False, Other };
    void Value(Kind k) {
        if (depth_ == 4 && inMessage_) {
            if (key_ == "media") media_ = k == Kind::True;
            else if (key_ == "meta") meta_ = k == Kind::True;
            else if (key_ == "sticker") sticker_ = k == Kind::True;
        }
    }
    void EndMessage() {
        ++chatMessages_;
        if (chatJid_ == kCallLogJid) {
            ++s_.calls;
            return;
        }
        ++s_.messages;
        if (!media_ && !sticker_) return;
        if (!hasData_ || data_.empty() || data_ == kMissing) {
            if (media_) ++s_.missingMedia;
            return;
        }
        ++s_.mediaPaths;
        std::string p = data_;
        for (char& c : p)
            if (c == '\\') c = '/';
        if (p.find("WhatsApp/Media/") != std::string::npos) ++s_.mediaPathsUnderMediaDir;
        std::string mime = ToLowerAscii(mime_);
        std::string ext = Extension(p);
        if (sticker_ || ext == "webp") ++s_.stickers;
        else if (StartsWith(mime, "image") || ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif") ++s_.photos;
        else if (StartsWith(mime, "video") || ext == "mp4" || ext == "3gp" || ext == "mov" || ext == "mkv") ++s_.videos;
        else if (StartsWith(mime, "audio") || ext == "opus" || ext == "ogg" || ext == "m4a" || ext == "mp3" ||
                 ext == "aac" || ext == "amr" || ext == "wav")
            ++s_.audio;
        else ++s_.documents;
    }

    ArchiveStats& s_;
    const CancelToken& cancel_;
    int depth_ = 0;
    std::uint64_t objects_ = 0;
    std::string key_, chatJid_, data_, mime_;
    std::uint64_t chatMessages_ = 0;
    bool inMessages_ = false, inMessage_ = false;
    bool media_ = false, meta_ = false, sticker_ = false, hasData_ = false;
};

}  // namespace

std::string EscapeLineSeparators(std::string_view chunk, std::string& carry, bool final,
                                 std::uint64_t* escapedCount) {
    std::string in = carry + std::string(chunk);
    carry.clear();
    std::string out;
    out.reserve(in.size() + 16);
    size_t i = 0;
    for (; i < in.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c != 0xE2) {
            out += in[i];
            continue;
        }
        if (i + 2 >= in.size() && !final) {
            carry = in.substr(i);  // may be the start of E2 80 A8/A9
            return out;
        }
        if (i + 2 < in.size() && static_cast<unsigned char>(in[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(in[i + 2]) == 0xA8 || static_cast<unsigned char>(in[i + 2]) == 0xA9)) {
            out += static_cast<unsigned char>(in[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            if (escapedCount) ++*escapedCount;
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

bool ScanChatsJson(const fs::path& json, ArchiveStats& stats, std::string& error, const CancelToken& cancel) {
    std::ifstream in(LongPath(json), std::ios::binary);
    if (!in) {
        error = "chats.json was not created";
        return false;
    }
    stats = {};
    StatsSax sax(stats, cancel);
    bool ok = false;
    try {
        ok = nlohmann::json::sax_parse(in, &sax, nlohmann::json::input_format_t::json, true);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (cancel.IsCancelled()) {
        error = "Cancelled";
        return false;
    }
    if (!ok) error = sax.error.empty() ? "chats.json is not valid JSON" : sax.error;
    return ok;
}

ChatsJsResult WriteChatsJs(const fs::path& json, const fs::path& js, const CancelToken& cancel) {
    ChatsJsResult r;
    if (!ScanChatsJson(json, r.stats, r.error, cancel)) return r;

    std::error_code ec;
    r.jsonBytes = fs::file_size(LongPath(json), ec);
    fs::path temp = js;
    temp += ".partial";
    {
        std::ifstream in(LongPath(json), std::ios::binary);
        std::ofstream out(LongPath(temp), std::ios::binary | std::ios::trunc);
        if (!in || !out) {
            r.error = "Cannot write chats.js";
            return r;
        }
        out << kChatsJsPrefix;
        std::vector<char> buf(1 << 20);
        std::string carry;
        while (in) {
            if (cancel.IsCancelled()) {
                out.close();
                fs::remove(LongPath(temp), ec);
                r.error = "Cancelled";
                return r;
            }
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            auto n = in.gcount();
            if (n <= 0) break;
            std::string piece = EscapeLineSeparators(std::string_view(buf.data(), static_cast<size_t>(n)), carry,
                                                     false, &r.escapedSeparators);
            out.write(piece.data(), static_cast<std::streamsize>(piece.size()));
        }
        std::string tail = EscapeLineSeparators({}, carry, true, &r.escapedSeparators);
        out.write(tail.data(), static_cast<std::streamsize>(tail.size()));
        out << kChatsJsSuffix;
        out.close();
        if (!out) {
            fs::remove(LongPath(temp), ec);
            r.error = "Writing chats.js failed (is the disk full?)";
            return r;
        }
    }
    std::uint64_t expected =
        std::strlen(kChatsJsPrefix) + r.jsonBytes + r.escapedSeparators * 3 + std::strlen(kChatsJsSuffix);
    std::uint64_t actual = fs::file_size(LongPath(temp), ec);
    if (ec || actual != expected) {
        fs::remove(LongPath(temp), ec);
        r.error = "chats.js has the wrong size (" + std::to_string(actual) + " instead of " +
                  std::to_string(expected) + ")";
        return r;
    }
    if (auto err = RenameWithRetry(temp, js)) {
        r.error = "Cannot finish chats.js: " + *err;
        return r;
    }
    r.jsBytes = actual;
    r.ok = true;
    return r;
}

}  // namespace ck
