// src/platform/ThemeManager.h
#pragma once
#include <filesystem>
class ThemeManager {
public:
    static ThemeManager& instance() { static ThemeManager m; return m; }
    void initialize(const std::filesystem::path& resourceDir) {}
};
