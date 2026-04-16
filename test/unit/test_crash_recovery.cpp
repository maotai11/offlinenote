#include "../catch_amalgamated.hpp"
#include "../../src/util/CrashRecovery.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("CrashRecovery identifies snapshot files and paths", "[crash-recovery]") {
    const fs::path notesDir = fs::path("C:/tmp/offlinenote");

    REQUIRE(CrashRecovery::snapshotPath(notesDir) == notesDir / "crash_recovery.onote");
    REQUIRE(CrashRecovery::sessionMarkerPath(notesDir) == notesDir / ".offlinenote-session-active");
    REQUIRE(CrashRecovery::isSnapshotFile(notesDir / "crash_recovery.onote"));
    REQUIRE_FALSE(CrashRecovery::isSnapshotFile(notesDir / "normal_note.onote"));
}

TEST_CASE("CrashRecovery startup decisions distinguish real recovery from stale files", "[crash-recovery]") {
    REQUIRE(CrashRecovery::shouldRestoreSnapshot(true, true));
    REQUIRE_FALSE(CrashRecovery::shouldRestoreSnapshot(true, false));
    REQUIRE_FALSE(CrashRecovery::shouldRestoreSnapshot(false, true));

    REQUIRE(CrashRecovery::shouldDeleteStaleSnapshot(true, false));
    REQUIRE_FALSE(CrashRecovery::shouldDeleteStaleSnapshot(true, true));
    REQUIRE_FALSE(CrashRecovery::shouldDeleteStaleSnapshot(false, true));
}

TEST_CASE("CrashRecovery recovered note names are explicit but stable", "[crash-recovery]") {
    REQUIRE(CrashRecovery::recoveredNoteName("") == "Recovered Note");
    REQUIRE(CrashRecovery::recoveredNoteName("Meeting Notes") == "Meeting Notes (Recovered)");
    REQUIRE(CrashRecovery::recoveredNoteName("Meeting Notes (Recovered)") == "Meeting Notes (Recovered)");
}
