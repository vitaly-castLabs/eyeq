#pragma once

#include <cmath>
#include <limits>
#include <optional>

#include "metrics.h"

class Psnr : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "PSNR"; }

    std::optional<float> measure(const Image& ref, const Image& dist) noexcept override {
        auto y_ref = ref.plane(0);
        auto y_dist = dist.plane(0);
        const size_t n = y_ref.size();

        float mse = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float diff = static_cast<float>(y_ref[i]) - static_cast<float>(y_dist[i]);
            mse += diff * diff;
        }
        mse /= static_cast<float>(n);

        if (mse < 1e-10f)
            return std::numeric_limits<float>::infinity();
        return 10.0f * std::log10((255.0f * 255.0f) / mse);
    }
};
