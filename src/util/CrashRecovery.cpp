// SPDX-License-Identifier: GPL-2.0-or-later

#include "CrashRecovery.h"

namespace CrashRecovery {

std::filesystem::path snapshotPath(const std::filesystem::path& notesDir)
{
    return notesDir / kSnapshotFileName;
}

std::filesystem::path sessionMarkerPath(const std::filesystem::path& notesDir)
{
    return notesDir / kSessionMarkerFileName;
}

bool isSnapshotFile(const std::filesystem::path& filePath)
{
    return filePath.filename() == kSnapshotFileName;
}

bool shouldRestoreSnapshot(bool snapshotExists, bool sessionMarkerExists)
{
    return snapshotExists && sessionMarkerExists;
}

bool shouldDeleteStaleSnapshot(bool snapshotExists, bool sessionMarkerExists)
{
    return snapshotExists && !sessionMarkerExists;
}

std::string recoveredNoteName(const std::string& originalName)
{
    static const std::string suffix = " (Recovered)";
    if (originalName.empty()) {
        return "Recovered Note";
    }
    if (originalName.size() >= suffix.size() &&
        originalName.compare(originalName.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return originalName;
    }
    return originalName + suffix;
}

} // namespace CrashRecovery
