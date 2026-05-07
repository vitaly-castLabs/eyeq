#pragma once

#include <optional>
#include <vector>

#include "metrics.h"
#include "vmaf.h"

class SsimBase : public Metrics {
public:
    using Metrics::Metrics;

protected:
    std::optional<float> compute(const Image& ref, const Image& dist, const char* feature) noexcept {
        if (colorspace_ != ColorSpace::I420)
            return std::nullopt;

        VmafSession runner;
        if (!runner.init())
            return std::nullopt;
        if (!runner.use_feature(feature))
            return std::nullopt;
        if (!runner.alloc_pictures(ref.width, ref.height))
            return std::nullopt;

        runner.copy_plane_data(ref, dist);
        if (!runner.feed_pictures(0))
            return std::nullopt;
        if (!runner.flush())
            return std::nullopt;
        return runner.get_feature_score(feature, 0);
    }
};

class Ssim : public SsimBase {
public:
    using SsimBase::SsimBase;

    const char* name() const override { return "SSIM"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override {
        auto s = compute(ref, dist, "float_ssim");
        if (!s)
            return std::nullopt;
        return std::vector<Score>{{"SSIM", *s}};
    }
};

class MsSsim : public SsimBase {
public:
    using SsimBase::SsimBase;

    const char* name() const override { return "MS-SSIM"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override {
        auto s = compute(ref, dist, "float_ms_ssim");
        if (!s)
            return std::nullopt;
        return std::vector<Score>{{"MS-SSIM", *s}};
    }
};
