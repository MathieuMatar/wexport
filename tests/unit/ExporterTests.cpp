#include <algorithm>
#include <fstream>
#include <sstream>

#include "Core/Exporter/ExporterCommand.h"
#include "Core/Exporter/ExporterOutput.h"
#include "Test.h"

using namespace ck;

namespace {
std::string ReadFixture(const char* name) {
    std::ifstream in(std::string(CK_FIXTURES_DIR) + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
bool Has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

TEST(ExporterArgsAreSafe) {
    ExporterOptions o;
    o.key = std::string(64, 'a');
    auto args = BuildExporterArgs(o);
    CHECK(Has(args, "--android"));
    for (const auto& x : args) CHECK(x != "-m" && x != "-c");  // the Nuitka build refuses "-m"
    CHECK(Has(args, "--no-banner"));
    CHECK(Has(args, "--no-html"));
    CHECK(!Has(args, "-c"));
    CHECK(!Has(args, "--move-media"));
    CHECK(!Has(args, "--check-update"));
    CHECK(!Has(args, "--wab"));
    CHECK(!Has(args, "--enrich-from-vcards"));
    auto k = std::find(args.begin(), args.end(), "--key");
    CHECK(k != args.end() && k + 1 != args.end() && *(k + 1) == o.key);
    auto m = std::find(args.begin(), args.end(), "--media");
    CHECK(*(m + 1) == "WhatsApp");
    auto out = std::find(args.begin(), args.end(), "--output");
    CHECK(*(out + 1) == ".");
    CHECK(DescribeExporterArgs(args).find(o.key) == std::string::npos);

    o.hasContactsBackup = true;
    o.vcf = std::filesystem::path("C:/x/contacts.vcf");
    o.countryCode = "961";
    args = BuildExporterArgs(o);
    CHECK(Has(args, "--wab"));
    CHECK(Has(args, "--enrich-from-vcards"));
    CHECK(Has(args, "--default-country-code"));
    CHECK(Has(args, "961"));
}

TEST(WindowsQuoting) {
    CHECK_EQ(QuoteWindowsArg("plain"), std::string("plain"));
    CHECK_EQ(QuoteWindowsArg("has space"), std::string("\"has space\""));
    CHECK_EQ(QuoteWindowsArg(""), std::string("\"\""));
    CHECK_EQ(QuoteWindowsArg("C:\\a b\\"), std::string("\"C:\\a b\\\\\""));
    CHECK_EQ(QuoteWindowsArg("say \"hi\""), std::string("\"say \\\"hi\\\"\""));
    CHECK_EQ(QuoteWindowsArg("C:\\no\\space"), std::string("C:\\no\\space"));
}

TEST(ParsesCapturedProgress) {
    std::vector<ExporterStatus> seen;
    ExporterOutputParser p([&](const ExporterStatus& s) { seen.push_back(s); });
    std::string text = ReadFixture("wtsexporter-0.13.0-output.txt");
    CHECK(!text.empty());
    // Feed in awkward chunks to exercise the line splitting.
    for (size_t i = 0; i < text.size(); i += 7) p.Feed(std::string_view(text).substr(i, 7));
    p.Finish();
    bool sawDecrypt = false, sawMessagesBar = false, sawCalls = false;
    for (const auto& s : seen) {
        if (s.stage == ExporterStage::Decrypting) sawDecrypt = true;
        if (s.stage == ExporterStage::Messages && s.fraction && *s.fraction == 0.0) sawMessagesBar = true;
        if (s.stage == ExporterStage::Calls) sawCalls = true;
    }
    CHECK(sawDecrypt);
    CHECK(sawMessagesBar);
    CHECK(sawCalls);
    CHECK(p.Status().stage == ExporterStage::Done);
    CHECK(p.Tail().find("Everything is done") != std::string::npos);
    CHECK(p.Tail().find("Processing messages:") == std::string::npos);  // bars aren't kept
    CHECK(ClassifyExporterResult(0, text) == ExporterFailure::None);
}

TEST(ParsesTqdmLine) {
    ExporterStatus s;
    CHECK(ExporterOutputParser::ParseSegment(
        "Processing messages:  42%|####2     | 26/63 [00:01<00:02, 20.1msg/s]", s));
    CHECK(s.stage == ExporterStage::Messages);
    CHECK(s.fraction && *s.fraction > 0.41 && *s.fraction < 0.42);
    CHECK(ExporterOutputParser::ParseSegment("[INFO] Processing calls...(2)", s));
    CHECK(s.stage == ExporterStage::Calls);
    CHECK(!s.fraction);
}

TEST(DetectsWrongKey) {
    std::string text = ReadFixture("wtsexporter-0.13.0-wrong-key.txt");
    CHECK(ClassifyExporterResult(1, text) == ExporterFailure::WrongKey);
    CHECK(ClassifyExporterResult(1, "Traceback (most recent call last):\nKeyError") == ExporterFailure::Crashed);
    CHECK(ClassifyExporterResult(6, "[ERROR] The message database does not exist.") ==
          ExporterFailure::MissingDatabase);
}

TEST(KeepsWindowsLineEndings) {
    // Python on Windows writes \r\n; those are lines, not tqdm redraws.
    ExporterOutputParser p;
    std::string text = "usage: wtsexporter [-h]\r\nwtsexporter: error: unrecognized arguments: --x\r\n"
                       "\rProcessing messages:   0%|          | 0/63 [00:00<?, ?msg/s]\r\r[INFO] Done line\r\n";
    for (size_t i = 0; i < text.size(); ++i) p.Feed(std::string_view(text).substr(i, 1));
    p.Finish();
    std::string tail = p.Tail();
    CHECK(tail.find("usage: wtsexporter") != std::string::npos);
    CHECK(tail.find("unrecognized arguments") != std::string::npos);
    CHECK(tail.find("Done line") != std::string::npos);
    CHECK(tail.find("Processing messages") == std::string::npos);
}
