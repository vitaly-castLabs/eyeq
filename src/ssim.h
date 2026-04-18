#pragma once

#include <optional>
#include <vector>

#include "metrics.h"
#include "vmaf.h"

class Ssim : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "SSIM"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override {
        if (colorspace_ != ColorSpace::I420)
            return std::nullopt;

        VmafSession runner;
        if (!runner.init())
            return std::nullopt;
        if (!runner.use_feature("float_ssim"))
            return std::nullopt;
        if (!runner.alloc_pictures(ref.width, ref.height))
            return std::nullopt;

        runner.copy_plane_data(ref, dist);
        if (!runner.feed_pictures(0))
            return std::nullopt;
        if (!runner.flush())
            return std::nullopt;
        auto score = runner.get_feature_score("float_ssim", 0);
        if (!score)
            return std::nullopt;
        return std::vector<Score>{{"SSIM", *score}};
    }
};
