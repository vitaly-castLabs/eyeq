#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// Returned by resolve_path. Auto-removes the temp file on destruction when owned.
class PathHolder {
public:
    PathHolder() = default;
    PathHolder(std::string p, bool owns): path_(std::move(p)), owns_(owns) {}
    PathHolder(const PathHolder&) = delete;
    PathHolder& operator=(const PathHolder&) = delete;
    PathHolder(PathHolder&& o) noexcept: path_(std::move(o.path_)), owns_(std::exchange(o.owns_, false)) {}
    PathHolder& operator=(PathHolder&& o) noexcept;
    ~PathHolder();

    const std::string& path() const { return path_; }

private:
    std::string path_;
    bool owns_ = false;
};

enum class PathMode {
    Auto,      // original path for image inputs, temp PPM for raw YUV
    ForcePpm,  // always materialize a temp PPM via I420 (forces chroma round-trip)
};

// For image-format inputs in Auto mode, returns the original path. For .yuv
// inputs, or whenever ForcePpm is requested, materializes a temp PPM (via
// I420->RGB24) that auto-deletes when the returned holder goes out of scope.
// Use this when feeding path-based loaders that can't be served from memory
// (e.g., libjxl's SetFromFile).
std::optional<PathHolder> resolve_path(const Image& img, PathMode mode = PathMode::Auto);
