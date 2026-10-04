#include "Vcf.h"

#include <cctype>
#include <fstream>
#include <sstream>

#include "../Util/FileSystem.h"
#include "../Util/Strings.h"

namespace ck {

namespace {

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// vCard 3.0 escapes: \, \; \n
std::string Unescape(std::string_view v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            out += (n == 'n' || n == 'N') ? ' ' : n;
        } else {
            out += v[i];
        }
    }
    return out;
}

std::vector<std::string> UnfoldLines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    size_t i = 0;
    if (StartsWith(text, "\xEF\xBB\xBF")) i = 3;
    bool qpContinuation = false;
    while (i <= text.size()) {
        size_t end = text.find('\n', i);
        if (end == std::string_view::npos) end = text.size();
        std::string line(text.substr(i, end - i));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        i = end + 1;
        if (qpContinuation) {
            current += line;  // quoted-printable soft line break: "=" at end of line
        } else if (!line.empty() && (line[0] == ' ' || line[0] == '\t') && !current.empty()) {
            current += line.substr(1);  // RFC folding
        } else {
            if (!current.empty()) lines.push_back(current);
            current = line;
        }
        std::string upperHead = ToLowerAscii(current.substr(0, current.find(':')));
        qpContinuation = upperHead.find("quoted-printable") != std::string::npos && !current.empty() &&
                         current.back() == '=';
        if (qpContinuation) current.pop_back();
        if (end == text.size()) break;
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

}  // namespace

std::string DecodeQuotedPrintable(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '=' && i + 2 < text.size()) {
            int hi = HexValue(text[i + 1]), lo = HexValue(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

std::vector<VcfContact> ParseVcf(std::string_view text) {
    std::vector<VcfContact> contacts;
    VcfContact current;
    bool inCard = false;
    std::string nName;
    for (const auto& line : UnfoldLines(text)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string head = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        std::string headLower = ToLowerAscii(head);
        // Strip an "item1." group prefix (Apple / Google style).
        std::string prop = headLower.substr(0, headLower.find(';'));
        if (auto dot = prop.find('.'); dot != std::string::npos) prop = prop.substr(dot + 1);

        if (prop == "begin" && EqualsIgnoreCase(Trim(value), "vcard")) {
            current = {};
            nName.clear();
            inCard = true;
            continue;
        }
        if (!inCard) continue;
        if (prop == "end") {
            if (current.name.empty()) current.name = nName;
            if (!current.phones.empty()) contacts.push_back(current);
            inCard = false;
            continue;
        }
        bool qp = headLower.find("quoted-printable") != std::string::npos;
        std::string decoded = qp ? DecodeQuotedPrintable(value) : value;
        if (prop == "fn") {
            current.name = Trim(Unescape(decoded));
        } else if (prop == "n" && nName.empty()) {
            // N:Family;Given;Middle;Prefix;Suffix -> "Given Middle Family"
            std::vector<std::string> parts;
            std::string part;
            for (size_t i = 0; i <= decoded.size(); ++i) {
                if (i == decoded.size() || (decoded[i] == ';' && (i == 0 || decoded[i - 1] != '\\'))) {
                    parts.push_back(Trim(Unescape(part)));
                    part.clear();
                } else {
                    part += decoded[i];
                }
            }
            std::string joined;
            for (size_t idx : {3u, 1u, 2u, 0u, 4u}) {
                if (idx < parts.size() && !parts[idx].empty()) joined += (joined.empty() ? "" : " ") + parts[idx];
            }
            nName = joined;
        } else if (prop == "tel") {
            std::string phone = Trim(decoded);
            if (StartsWith(ToLowerAscii(phone), "tel:")) phone = phone.substr(4);
            if (!phone.empty()) current.phones.push_back(phone);
        }
    }
    return contacts;
}

bool LooksLikeVcf(const std::filesystem::path& file) {
    std::ifstream in(LongPath(file), std::ios::binary);
    if (!in) return false;
    char buf[512] = {};
    in.read(buf, sizeof buf - 1);
    std::string_view head(buf, static_cast<size_t>(in.gcount()));
    if (StartsWith(head, "\xEF\xBB\xBF")) head.remove_prefix(3);
    while (!head.empty() && std::isspace(static_cast<unsigned char>(head.front()))) head.remove_prefix(1);
    return EqualsIgnoreCase(head.substr(0, 11), "BEGIN:VCARD");
}

std::string NormalizePhone(std::string_view raw, std::string_view countryCode) {
    std::string trimmed = Trim(raw);
    bool plus = StartsWith(trimmed, "+");
    std::string digits;
    for (char c : trimmed)
        if (c >= '0' && c <= '9') digits += c;
    if (digits.empty()) return "";
    if (plus) return digits;
    if (StartsWith(digits, "00")) return digits.substr(2);
    if (digits[0] == '0') return std::string(countryCode) + digits.substr(1);
    if (digits.size() <= 8) return std::string(countryCode) + digits;
    return digits;
}

std::map<std::string, std::string> VcfPhoneBook(const std::filesystem::path& file, std::string_view countryCode) {
    std::map<std::string, std::string> book;
    std::ifstream in(LongPath(file), std::ios::binary);
    if (!in) return book;
    std::stringstream ss;
    ss << in.rdbuf();
    for (const auto& c : ParseVcf(ss.str())) {
        if (c.name.empty()) continue;
        for (const auto& p : c.phones) {
            std::string n = NormalizePhone(p, countryCode);
            if (n.size() > 5) book.emplace(n, c.name);
        }
    }
    return book;
}

}  // namespace ck
