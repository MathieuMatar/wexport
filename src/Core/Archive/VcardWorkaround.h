#pragma once
// Works around a wtsexporter 0.13.x crash (ExporterFailure::VcardChatMissing):
// makes a copy of the decrypted message database without the contact-card
// rows the exporter can't place, so it can be run again on the copy. The
// original database is never modified; the affected messages keep their text
// (the shared contact's name).

#include <filesystem>
#include <optional>
#include <string>

namespace ck {

inline constexpr const char* kWorkaroundDb = "_work/msgstore-export.db";

enum class VcardWorkaroundLevel {
    LidChats,  // only contact cards in chats known by a privacy ID (LID)
    All,       // every contact card (fallback)
};

struct VcardWorkaroundResult {
    bool ok = false;
    std::string error;
    long long removed = 0;
};

VcardWorkaroundResult MakeVcardWorkaroundDb(const std::filesystem::path& decryptedDb,
                                            const std::filesystem::path& copy, VcardWorkaroundLevel level);

}  // namespace ck
