// src/util/StringUtils.h
// UTF-8 字串工具
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>

namespace StringUtils {
    std::string trim(const std::string& str);
    std::string toLower(const std::string& str);
    std::string toUpper(const std::string& str);
    bool startsWith(const std::string& str, const std::string& prefix);
    bool endsWith(const std::string& str, const std::string& suffix);
    std::vector<std::string> split(const std::string& str, char delimiter);
    std::string join(const std::vector<std::string>& parts, const std::string& delimiter);
}
