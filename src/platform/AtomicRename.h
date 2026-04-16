// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>

bool atomicRename(const std::filesystem::path& src, const std::filesystem::path& dest);
