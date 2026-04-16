#pragma once

#include <filesystem>
#include <string>

namespace CrashRecovery {

constexpr const char* kSnapshotFileName = "crash_recovery.onote";
constexpr const char* kSessionMarkerFileName = ".offlinenote-session-active";

std::filesystem::path snapshotPath(const std::filesystem::path& notesDir);
std::filesystem::path sessionMarkerPath(const std::filesystem::path& notesDir);
bool isSnapshotFile(const std::filesystem::path& filePath);
bool shouldRestoreSnapshot(bool snapshotExists, bool sessionMarkerExists);
bool shouldDeleteStaleSnapshot(bool snapshotExists, bool sessionMarkerExists);
std::string recoveredNoteName(const std::string& originalName);

} // namespace CrashRecovery
