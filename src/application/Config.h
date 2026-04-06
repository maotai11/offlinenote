// src/application/Config.h
// 應用設定讀寫
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <map>
#include <filesystem>

class Config {
public:
    static Config& instance();

    void load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path);

    std::string get(const std::string& section, const std::string& key, const std::string& defaultVal = "") const;
    void set(const std::string& section, const std::string& key, const std::string& value);

private:
    Config() = default;
    // section -> key -> value
    std::map<std::string, std::map<std::string, std::string>> values_;
};
