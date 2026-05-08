#pragma once

#include "metrics.h"

class Dssim: public Metrics {
public:
    using Metrics::Metrics;
    const char* name() const override { return "DSSIM"; }
    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};
