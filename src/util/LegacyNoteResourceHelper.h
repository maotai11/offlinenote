// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace LegacyNoteResourceHelper {

std::string sanitizeStoredPath(std::string_view rawPath);
std::filesystem::path resolveForNote(const std::filesystem::path& notePath, std::string_view storedPath);
std::string stageForNote(const std::filesystem::path& targetNotePath,
                         const std::filesystem::path& resourceRef,
                         const std::filesystem::path& sourceNotePath = {});

} // namespace LegacyNoteResourceHelper
