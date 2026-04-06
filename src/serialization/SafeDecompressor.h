// src/serialization/SafeDecompressor.h
#pragma once
#include <filesystem>
#include <vector>
#include <string>
struct DecompressResult {
    std::vector<char> data;
    bool success = false;
    std::string errorMessage;
};
class SafeDecompressor {
public:
    static DecompressResult decompress(const std::filesystem::path& path);
};
