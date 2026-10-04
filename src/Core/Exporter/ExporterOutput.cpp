#include "ExporterOutput.h"

#include <cctype>
#include <cstdlib>
#include <regex>

#include "../Util/Strings.h"

namespace ck {

namespace {

std::string StripAnsi(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

ExporterStage StageForWord(const std::string& word) {
    std::string w = ToLowerAscii(word);
    if (w == "contacts") return ExporterStage::Contacts;
    if (w == "messages") return ExporterStage::Messages;
    if (w == "reactions") return ExporterStage::Reactions;
    if (w == "media") return ExporterStage::Media;
    if (w == "vcards") return ExporterStage::VCards;
    if (w == "calls") return ExporterStage::Calls;
    return ExporterStage::Starting;
}

}  // namespace

bool ExporterOutputParser::ParseSegment(std::string_view raw, ExporterStatus& st) {
    std::string s = Trim(StripAnsi(raw));
    if (s.empty()) return false;

    // tqdm: "Processing messages:  42%|####      | 26/63 [00:00<00:00, 1.2msg/s]"
    static const std::regex kBar(R"(^Processing (\w+):\s*(\d+)%\|[^|]*\|\s*(\d+)/(\d+))");
    // tqdm without a total: "Processing vCards: 0vcard [00:00, ?vcard/s]"
    static const std::regex kBarNoTotal(R"(^Processing (\w+):\s*\d+\w+\s*\[)");
    static const std::regex kProcessingInfo(R"(^\[INFO\] Processing (\w+)\.\.\.)");
    static const std::regex kProcessed(R"(^\[INFO\] Processed \d+ (\w+))");
    std::smatch m;

    if (std::regex_search(s, m, kBar)) {
        st.stage = StageForWord(m[1]);
        double done = std::strtod(m[3].str().c_str(), nullptr);
        double total = std::strtod(m[4].str().c_str(), nullptr);
        st.fraction = total > 0 ? done / total : std::optional<double>{};
        st.line = "Processing " + m[1].str() + " (" + m[3].str() + " of " + m[4].str() + ")";
        return true;
    }
    if (std::regex_search(s, m, kBarNoTotal)) {
        st.stage = StageForWord(m[1]);
        st.fraction.reset();
        st.line = "Processing " + m[1].str();
        return true;
    }
    if (s.find("decrypting WhatsApp backup") != std::string::npos) {
        st.stage = ExporterStage::Decrypting;
        st.fraction.reset();
        st.line = "Decrypting backup";
        return true;
    }
    if (std::regex_search(s, m, kProcessingInfo)) {
        st.stage = StageForWord(m[1]);
        st.fraction.reset();
        st.line = s.substr(7);
        return true;
    }
    if (std::regex_search(s, m, kProcessed)) {
        st.stage = StageForWord(m[1]);
        st.fraction = 1.0;
        st.line = s.substr(7);
        return true;
    }
    if (StartsWith(s, "[INFO] Writing JSON file")) {
        st.stage = ExporterStage::WritingJson;
        st.fraction.reset();
        st.line = "Writing JSON file";
        return true;
    }
    if (StartsWith(s, "[INFO] JSON file saved")) {
        st.stage = ExporterStage::WritingJson;
        st.fraction = 1.0;
        st.line = s.substr(7);
        return true;
    }
    if (StartsWith(s, "[INFO] Everything is done")) {
        st.stage = ExporterStage::Done;
        st.fraction = 1.0;
        st.line = "Done";
        return true;
    }
    if (StartsWith(s, "[INFO] ") || StartsWith(s, "[WARNING] ")) {
        st.line = s.substr(s.find(']') + 2);
        return true;
    }
    return false;
}

void ExporterOutputParser::Segment(std::string_view segment, bool lineEnded) {
    std::string clean = Trim(StripAnsi(segment));
    if (clean.empty()) return;
    bool isBar = StartsWith(clean, "Processing ") && clean.find('|') != std::string::npos;
    bool isBarNoTotal = StartsWith(clean, "Processing ") && clean.find('[') != std::string::npos;
    if (lineEnded || (!isBar && !isBarNoTotal && StartsWith(clean, "["))) {
        if (!isBar && !isBarNoTotal) {
            tail_.push_back(clean);
            if (tail_.size() > 200) tail_.pop_front();
        }
    }
    if (ParseSegment(clean, status_) && onStatus_) onStatus_(status_);
}

void ExporterOutputParser::Feed(std::string_view chunk) {
    full_.append(chunk);
    if (full_.size() > (8u << 20)) full_.erase(0, full_.size() - (4u << 20));
    for (char c : chunk) {
        if (c == '\n') {
            Segment(pending_, true);
            pending_.clear();
        } else if (c == '\r') {
            Segment(pending_, false);
            pending_.clear();
        } else {
            pending_ += c;
        }
    }
}

void ExporterOutputParser::Finish() {
    if (!pending_.empty()) Segment(pending_, true);
    pending_.clear();
}

std::string ExporterOutputParser::Tail(std::size_t maxLines) const {
    std::string out;
    std::size_t start = tail_.size() > maxLines ? tail_.size() - maxLines : 0;
    for (std::size_t i = start; i < tail_.size(); ++i) {
        out += tail_[i];
        out += '\n';
    }
    return out;
}

ExporterFailure ClassifyExporterResult(int exitCode, std::string_view output) {
    auto has = [&](std::string_view needle) { return output.find(needle) != std::string_view::npos; };
    // 0.13.0 raises DecryptionError("... Decryption/Authentication failed. Ensure you are
    // using the correct key.") from AES-GCM tag verification, and exits with 1.
    if (has("Decryption/Authentication failed") || has("Ensure you are using the correct key"))
        return ExporterFailure::WrongKey;
    if (has("not a valid compressed stream") || has("not a SQLite database") || has("must be at least 131 bytes"))
        return ExporterFailure::NotABackup;
    if (has("The message database does not exist")) return ExporterFailure::MissingDatabase;
    if (exitCode == 0 && has("Everything is done")) return ExporterFailure::None;
    if (exitCode == 0) return ExporterFailure::None;
    if (has("Traceback (most recent call last)")) return ExporterFailure::Crashed;
    return ExporterFailure::Other;
}

}  // namespace ck
