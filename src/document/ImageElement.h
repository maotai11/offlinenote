// src/document/ImageElement.h
#pragma once
#include <vector>
#include <cstdint>
struct ImageElement {
    double x = 0, y = 0, width = 100, height = 100, rotation = 0;
    std::vector<uint8_t> imageData;
};
