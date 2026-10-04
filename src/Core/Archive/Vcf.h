#pragma once
// Minimal vCard reader for contact names (FN + TEL), as exported by Google
// Contacts and Android's Contacts app (vCard 2.1 and 3.0).

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ck {

struct VcfContact {
    std::string name;
    std::vector<std::string> phones;  // as written in the file
};

std::vector<VcfContact> ParseVcf(std::string_view text);

// True if the file starts with BEGIN:VCARD (after an optional BOM/whitespace).
bool LooksLikeVcf(const std::filesystem::path& file);

// Digits only; "00" prefix dropped; a leading "0" (or a number of 8 digits or
// fewer without one) gets the country code. Numbers written with "+" are kept.
std::string NormalizePhone(std::string_view raw, std::string_view countryCode);

// phone digits -> name, first name wins.
std::map<std::string, std::string> VcfPhoneBook(const std::filesystem::path& file, std::string_view countryCode);

std::string DecodeQuotedPrintable(std::string_view text);

}  // namespace ck
