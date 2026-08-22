#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Decodes a PNG file to tightly packed 8-bit RGBA.
// Returns false on failure.
bool loadPNG(const char* path, std::vector<uint8_t>& rgba, int& w, int& h);

// Encodes tightly packed 32bpp BGRA pixels to a PNG file.
bool savePNG(const std::string& path, int w, int h, const std::vector<uint8_t>& bgra);
