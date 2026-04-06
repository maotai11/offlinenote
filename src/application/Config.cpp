// src/application/Config.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Config.h"
#include <fstream>
#include <sstream>

Config& Config::instance() {
    static Config instance;
    return instance;
}

void Config::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string currentSection;
    std::string line;
    while (std::getline(file, line)) {
        // Trim
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            auto end = line.find(']');
            if (end != std::string::npos)
                currentSection = line.substr(1, end - 1);
        }
        else {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                // Trim key/value
                while (!key.empty() && key.back() == ' ') key.pop_back();
                while (!val.empty() && val.front() == ' ') val = val.substr(1);
                values_[currentSection][key] = val;
            }
        }
    }
}

void Config::save(const std::filesystem::path& path) {
    std::ofstream file(path);
    if (!file.is_open()) return;
    for (const auto& [section, kvMap] : values_) {
        file << "[" << section << "]\n";
        for (const auto& [key, val] : kvMap) {
            file << key << " = " << val << "\n";
        }
        file << "\n";
    }
}

std::string Config::get(const std::string& section, const std::string& key, const std::string& defaultVal) const {
    auto sit = values_.find(section);
    if (sit == values_.end()) return defaultVal;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return defaultVal;
    return kit->second;
}

void Config::set(const std::string& section, const std::string& key, const std::string& value) {
    values_[section][key] = value;
}
