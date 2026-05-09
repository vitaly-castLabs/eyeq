#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "image.h"

bool is_raw_yuv_path(std::string_view path);

struct Rgb24 {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

// Loads an RGB24 view of the input image. For path-backed inputs (PNG/JPEG/...)
// the original file is decoded so RGB-space metrics see full chroma resolution.
// For raw .yuv inputs, the in-memory I420 buffer is converted via sws_scale.
std::optional<Rgb24> load_rgb24(const Image& img);
