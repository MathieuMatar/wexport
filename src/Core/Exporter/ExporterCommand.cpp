#include "ExporterCommand.h"

#include "../Util/Strings.h"

namespace ck {

namespace {
std::string Native(const char* forwardSlashPath) {
#ifdef _WIN32
    std::string s = forwardSlashPath;
    for (char& c : s)
        if (c == '/') c = '\\';
    return s;
#else
    return forwardSlashPath;
#endif
}
}  // namespace

// Long option names only: the official Windows build (Nuitka) refuses to run
// when its command line contains "-m" ("the program tried to call itself with
// '-m' argument"), so "-m WhatsApp" must be written "--media WhatsApp".
std::vector<std::string> BuildExporterArgs(const ExporterOptions& o) {
    std::vector<std::string> a = {"--android", "--no-banner", "--key", o.key, "--backup",
                                  Native("_work/msgstore.db.crypt15")};
    if (o.hasContactsBackup) {
        a.push_back("--wab");
        a.push_back(Native("_work/wa.db.crypt15"));
    }
    a.insert(a.end(), {"--db", Native(kMessagesDb), "--wa", Native(kContactsDb), "--media", "WhatsApp", "--output",
                       ".", "--json", Native(kChatsJson), "--no-html"});
    if (o.vcf && !o.countryCode.empty()) {
        a.push_back("--enrich-from-vcards");
        a.push_back(PathToUtf8(*o.vcf));
        a.push_back("--default-country-code");
        a.push_back(o.countryCode);
    }
    return a;
}

std::string DescribeExporterArgs(const std::vector<std::string>& args) {
    std::string out = "wtsexporter";
    for (size_t i = 0; i < args.size(); ++i) {
        bool isKey = i > 0 && (args[i - 1] == "-k" || args[i - 1] == "--key");
        out += ' ';
        out += isKey ? "<key redacted>" : QuoteWindowsArg(args[i]);
    }
    return out;
}

std::string QuoteWindowsArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') { ++i; ++backslashes; }
        if (i == arg.size()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out += '"';
        } else {
            out.append(backslashes, '\\');
            out += arg[i];
        }
    }
    out += '"';
    return out;
}

std::string JoinWindowsCommandLine(const std::string& exe, const std::vector<std::string>& args) {
    std::string line = QuoteWindowsArg(exe);
    for (const auto& a : args) line += " " + QuoteWindowsArg(a);
    return line;
}

}  // namespace ck
