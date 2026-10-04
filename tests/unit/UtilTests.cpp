#include "Core/Util/CountryCodes.h"
#include "Core/Util/FileSystem.h"
#include "Core/Util/Log.h"
#include "Core/Util/Strings.h"
#include "Test.h"

using namespace ck;

TEST(KeyAcceptsPastedForms) {
    std::string key = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    CHECK_EQ(*NormalizeKey(key), key);
    CHECK_EQ(*NormalizeKey("0123 4567 89AB CDEF 0123 4567 89ab cdef\r\n0123-4567-89ab-cdef 0123 4567 89ab cdef"), key);
    CHECK(!NormalizeKey(key.substr(1)));
    CHECK(!NormalizeKey(key + "0"));
    CHECK(!NormalizeKey("g" + key.substr(1)));
    CHECK_EQ(HexDigitsOnly("ab-CD xyz 12"), std::string("abcd12"));
}

TEST(SanitizeFileNames) {
    CHECK_EQ(SanitizeFileName("a<b>c:d\"e/f\\g|h?i*j"), std::string("a_b_c_d_e_f_g_h_i_j"));
    CHECK_EQ(SanitizeFileName("trailing. . "), std::string("trailing"));
    CHECK_EQ(SanitizeFileName("CON"), std::string("_CON"));
    CHECK_EQ(SanitizeFileName("nul.txt"), std::string("_nul.txt"));
    CHECK_EQ(SanitizeFileName("console"), std::string("console"));
    CHECK_EQ(SanitizeFileName(""), std::string("_"));
    CHECK_EQ(SanitizeFileName("Report \xC3\xA9t\xC3\xA9.pdf"), std::string("Report \xC3\xA9t\xC3\xA9.pdf"));
}

TEST(ExportFolderName) {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 4;
    tm.tm_hour = 23;
    tm.tm_min = 31;
    CHECK_EQ(MakeExportFolderName("Galaxy J6", tm), std::string("WhatsApp Export Galaxy J6 2026-10-04 23-31"));
    CHECK_EQ(MakeExportFolderName("My: Phone?", tm), std::string("WhatsApp Export My_ Phone_ 2026-10-04 23-31"));
}

TEST(Formatting) {
    CHECK_EQ(FormatCount(8910), std::string("8,910"));
    CHECK_EQ(FormatCount(1234567), std::string("1,234,567"));
    CHECK_EQ(FormatCount(12), std::string("12"));
    CHECK_EQ(FormatBytes(6871947674ull), std::string("6.4 GB"));
    CHECK_EQ(FormatBytes(2254857830ull), std::string("2.1 GB"));
    CHECK_EQ(FormatBytes(512), std::string("512 bytes"));
    CHECK_EQ(FormatTimeLeft(700), std::string("about 12 min left"));
    CHECK_EQ(FormatTimeLeft(30), std::string("less than a minute left"));
}

TEST(Utf8WideRoundTrip) {
    std::string s = "P\xC3\xA8re \xF0\x9F\x91\x8B \xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A";
    CHECK_EQ(ToUtf8(ToWide(s)), s);
}

TEST(LogRedactsKey) {
    std::string key = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    std::string spaced = "0123 4567 89ab cdef 0123 4567 89ab cdef 0123 4567 89ab cdef 0123 4567 89ab cdef";
    CHECK(RedactSecrets("-k " + key + " -b x", {}).find(key) == std::string::npos);
    CHECK(RedactSecrets("key " + spaced, {}).find("cdef 0123") == std::string::npos);
    CHECK_EQ(RedactSecrets("size 1234 abc", {}), std::string("size 1234 abc"));
    CHECK_EQ(RedactSecrets("secret here", {"secret"}), std::string("<key redacted> here"));
    Logger log;
    log.AddSecret("hunter2");
    log.Info("pw=hunter2 key=" + key);
    auto lines = log.Lines();
    CHECK(lines.back().find("hunter2") == std::string::npos);
    CHECK(lines.back().find(key) == std::string::npos);
}

TEST(SpaceAndOneDrive) {
    CHECK_EQ(RequiredSpace(1000ull * 1024 * 1024), 1000ull * 1024 * 1024 + 100ull * 1024 * 1024 + 500ull * 1024 * 1024);
    auto dir = t::TempDir("space");
    CHECK(FreeSpace(dir / "not" / "yet" / "there").has_value());
    CHECK(IsInside(dir / "a" / "b", dir));
    CHECK(!IsInside(dir.parent_path(), dir));
    CHECK(IsInsideAny(dir / "x", {dir.parent_path()}));
}

TEST(CountryCodes) {
    CHECK_EQ(CallingCodeForRegion("LB"), std::string("961"));
    CHECK_EQ(CallingCodeForRegion("fr"), std::string("33"));
    CHECK_EQ(CallingCodeForRegion("ZZ"), std::string(""));
    CHECK(Countries().size() > 150);
}
