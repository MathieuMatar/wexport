#pragma once
// Understands wtsexporter's console output. Built from a captured run of
// 0.13.0 (see tests/fixtures/wtsexporter-0.13.0-output.txt):
//   [INFO] Decryption key specified, decrypting WhatsApp backup...
//   \rProcessing messages:  42%|####      | 26/63 [00:00<00:00, ...]
//   [INFO] Processed 63 messages in less than a second
//   [INFO] Processing calls...(2)
//   [INFO] Writing JSON file...\r[INFO] JSON file saved...(25.97 KB)
// Everything goes to stderr; tqdm redraws with \r.

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ck {

enum class ExporterStage { Starting, Decrypting, Contacts, Messages, Reactions, Media, VCards, Calls, WritingJson, Done };

struct ExporterStatus {
    ExporterStage stage = ExporterStage::Starting;
    std::optional<double> fraction;  // within the stage, when a tqdm bar gives one
    std::string line;                // latest status line, cleaned up
};

// Feed raw output chunks; complete lines/redraws are parsed as they arrive.
class ExporterOutputParser {
public:
    using Callback = std::function<void(const ExporterStatus&)>;
    explicit ExporterOutputParser(Callback onStatus = {}) : onStatus_(std::move(onStatus)) {}

    void Feed(std::string_view chunk);
    void Finish();

    const ExporterStatus& Status() const { return status_; }
    // Last lines of output (not the tqdm redraws), for the error details.
    std::string Tail(std::size_t maxLines = 25) const;
    const std::string& FullText() const { return full_; }

    static bool ParseSegment(std::string_view segment, ExporterStatus& status);

private:
    void Segment(std::string_view segment, bool lineEnded);

    Callback onStatus_;
    ExporterStatus status_;
    std::string pending_;
    std::string full_;
    std::deque<std::string> tail_;
};

enum class ExporterFailure { None, WrongKey, NotABackup, MissingDatabase, Crashed, Other };

ExporterFailure ClassifyExporterResult(int exitCode, std::string_view output);

}  // namespace ck
