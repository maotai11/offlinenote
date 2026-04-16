// test/unit/test_file_lock.cpp
#include "../catch_amalgamated.hpp"
#include "../../src/platform/FileLock.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("FileLock prevents concurrent acquisition on the same note file", "[filesystem][lock]") {
    const fs::path notePath = fs::current_path() / "locked_note.onote";
    const fs::path lockPath = notePath.string() + ".lock";

    std::error_code ec;
    fs::remove(notePath, ec);
    fs::remove(lockPath, ec);

    FileLock first;
    REQUIRE(first.tryLock(notePath) == FileLockResult::Acquired);
    REQUIRE(first.isLocked());
    REQUIRE(fs::exists(lockPath));
    REQUIRE_FALSE(first.getLockHolderInfo().empty());

    FileLock second;
    REQUIRE(second.tryLock(notePath) == FileLockResult::AlreadyLocked);
    REQUIRE_FALSE(second.isLocked());
    REQUIRE_FALSE(second.getLockHolderInfo().empty());

    first.unlock();

    REQUIRE_FALSE(first.isLocked());
    REQUIRE_FALSE(fs::exists(lockPath));

    REQUIRE(second.tryLock(notePath) == FileLockResult::Acquired);
    REQUIRE(second.isLocked());

    second.unlock();
    REQUIRE_FALSE(fs::exists(lockPath));
}
