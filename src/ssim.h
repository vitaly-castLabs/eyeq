#pragma once

#include <optional>

#include "metrics.h"

class Ssim : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "SSIM"; }

    std::optional<float> measure(const Image& ref, const Image& dist) noexcept override {
        const int w = ref.width;
        const int h = ref.height;
        const auto& ref_data = ref.plane(0);
        const auto& dist_data = dist.plane(0);

        constexpr float C1 = (0.01f * 255.0f) * (0.01f * 255.0f);
        constexpr float C2 = (0.03f * 255.0f) * (0.03f * 255.0f);

        float ssim_sum = 0.0f;
        int count = 0;
        constexpr int win = 8;

        for (int y = 0; y <= h - win; ++y) {
            for (int x = 0; x <= w - win; ++x) {
                float mu_x = 0, mu_y = 0;
                for (int dy = 0; dy < win; ++dy)
                    for (int dx = 0; dx < win; ++dx) {
                        const int idx = (y + dy) * w + (x + dx);
                        mu_x += ref_data[idx];
                        mu_y += dist_data[idx];
                    }
                const int N = win * win;
                mu_x /= N;
                mu_y /= N;

                float sig_x = 0, sig_y = 0, sig_xy = 0;
                for (int dy = 0; dy < win; ++dy)
                    for (int dx = 0; dx < win; ++dx) {
                        const int idx = (y + dy) * w + (x + dx);
                        const float dx_x = ref_data[idx] - mu_x;
                        const float dx_y = dist_data[idx] - mu_y;
                        sig_x += dx_x * dx_x;
                        sig_y += dx_y * dx_y;
                        sig_xy += dx_x * dx_y;
                    }
                sig_x /= N - 1;
                sig_y /= N - 1;
                sig_xy /= N - 1;

                const float num = (2 * mu_x * mu_y + C1) * (2 * sig_xy + C2);
                const float den = (mu_x * mu_x + mu_y * mu_y + C1) * (sig_x + sig_y + C2);
                ssim_sum += num / den;
                ++count;
            }
        }

        if (count == 0)
            return 0.0f;
        return ssim_sum / count;
    }
};
