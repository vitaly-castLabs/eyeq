#pragma once

#include <cmath>
#include <optional>
#include <vector>

#include "metrics.h"
#include "vmaf.h"

class PsnrBase : public Metrics {
public:
    using Metrics::Metrics;

protected:
    struct Planes {
        float y, cb, cr;
    };

    std::optional<Planes> compute(const Image& ref, const Image& dist) noexcept {
        if (colorspace_ != ColorSpace::I420)
            return std::nullopt;

        VmafSession runner;
        if (!runner.init())
            return std::nullopt;
        if (!runner.use_feature("psnr"))
            return std::nullopt;
        if (!runner.alloc_pictures(ref.width, ref.height))
            return std::nullopt;

        runner.copy_plane_data(ref, dist);
        if (!runner.feed_pictures(0))
            return std::nullopt;
        if (!runner.flush())
            return std::nullopt;

        auto y = runner.get_feature_score("psnr_y", 0);
        auto cb = runner.get_feature_score("psnr_cb", 0);
        auto cr = runner.get_feature_score("psnr_cr", 0);
        if (!y || !cb || !cr)
            return std::nullopt;
        return Planes{*y, *cb, *cr};
    }
};

class PsnrY : public PsnrBase {
public:
    using PsnrBase::PsnrBase;

    const char* name() const override { return "PSNR (Y)"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override {
        auto p = compute(ref, dist);
        if (!p)
            return std::nullopt;
        return std::vector<Score>{{"PSNR (Y)", p->y}};
    }
};

class Psnr : public PsnrBase {
public:
    using PsnrBase::PsnrBase;

    const char* name() const override { return "PSNR"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override {
        auto p = compute(ref, dist);
        if (!p)
            return std::nullopt;

        // YUV 4:2:0 sample weights: Y=4, Cb=1, Cr=1 per 6 samples.
        auto mse_from_psnr = [](float q) { return (255.0f * 255.0f) / std::pow(10.0f, q / 10.0f); };
        const float mse_full = (4.0f * mse_from_psnr(p->y) + mse_from_psnr(p->cb) + mse_from_psnr(p->cr)) / 6.0f;
        const float psnr_full = 10.0f * std::log10((255.0f * 255.0f) / mse_full);
        return std::vector<Score>{{"PSNR", psnr_full}};
    }
};
