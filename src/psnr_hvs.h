#pragma once

#include <optional>

#include "image.h"
#include "vmaf.h"

class PsnrHvs : public Metrics {
public:
    PsnrHvs(int w, int h, ColorSpace cs): Metrics(w, h, cs) {}

    const char* name() const override { return "PSNR-HVS"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override {
        if (colorspace_ != ColorSpace::I420)
            return std::nullopt;

        VmafSession runner;
        if (!runner.init())
            return std::nullopt;
        if (!runner.use_feature("psnr_hvs"))
            return std::nullopt;
        if (!runner.alloc_pictures(ref.width, ref.height))
            return std::nullopt;

        runner.copy_plane_data(ref, dist);
        if (!runner.feed_pictures(0))
            return std::nullopt;
        if (!runner.flush())
            return std::nullopt;
        auto score = runner.get_feature_score("psnr_hvs", 0);
        if (!score)
            return std::nullopt;
        return std::vector<Score>{{"PSNR-HVS", *score}};
    }
};
