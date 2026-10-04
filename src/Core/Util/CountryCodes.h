#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ck {

struct Country {
    std::string iso2;  // "LB"
    std::string name;  // "Lebanon" (English; the UI shows it next to the code)
    std::string code;  // "961"
};

const std::vector<Country>& Countries();  // sorted by English name

// "LB" -> "961"; "" if unknown.
std::string CallingCodeForRegion(std::string_view iso2);

}  // namespace ck
