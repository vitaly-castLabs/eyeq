#pragma once

#include <optional>
#include <vector>

#include "metrics.h"

class Butteraugli : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "Butteraugli"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};
