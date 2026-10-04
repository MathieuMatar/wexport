#pragma once
// members.js for the viewer's group members panel (§6.3).

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../Util/Log.h"

namespace ck {

struct GroupMember {
    std::string name;   // "" if unknown
    std::string phone;  // digits, "" if hidden
    int rank = 0;       // 0 member, 1 admin, 2 creator
};

using GroupMembers = std::map<std::string, std::vector<GroupMember>>;  // group id (JID user) -> members

struct MembersInput {
    std::filesystem::path messagesDb;
    std::optional<std::filesystem::path> contactsDb;
    std::optional<std::filesystem::path> vcf;
    std::string countryCode;
};

// Detects the schema (group_participant_user, else group_participants) and
// degrades gracefully. Returns nothing if no members table could be read.
std::optional<GroupMembers> BuildGroupMembers(const MembersInput& input, Logger* log);

// Admin rank descending, named before unnamed (hidden numbers last), then by
// name, then by phone.
void SortMembers(std::vector<GroupMember>& members);

std::string MembersToJs(const GroupMembers& groups);

// Writes members.js via a temp file. Returns false (and writes nothing) if
// members couldn't be built.
bool WriteMembersJs(const MembersInput& input, const std::filesystem::path& out, Logger* log);

}  // namespace ck
