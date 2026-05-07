#pragma once

#include <optional>
#include <vector>

#include "metrics.h"

class Xpsnr : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "XPSNR"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};
