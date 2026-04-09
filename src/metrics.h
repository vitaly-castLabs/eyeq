#pragma once

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "image.h"

class Metrics {
public:
    Metrics(int w, int h, ColorSpace cs): width_(w), height_(h), colorspace_(cs) {}
    virtual ~Metrics() = default;

    virtual const char* name() const = 0;
    virtual std::optional<float> measure(const Image& ref, const Image& dist) noexcept = 0;

protected:
    int width_;
    int height_;
    ColorSpace colorspace_;
};

#include "psnr.h"
#include "psnr_hvs.h"
#include "ssim.h"
#include "vmaf.h"

class MetricsFactory {
public:
    static std::unique_ptr<Metrics> create(std::string_view metric, ColorSpace cs, int width, int height) {
        if (metric == "psnr")
            return std::make_unique<Psnr>(width, height, cs);
        if (metric == "ssim")
            return std::make_unique<Ssim>(width, height, cs);
        if (metric == "psnr-hvs")
            return std::make_unique<PsnrHvs>(width, height, cs);
        if (metric == "vmaf")
            return std::make_unique<Vmaf>(width, height, cs);
        return nullptr;
    }
};
