#pragma once

#include <optional>
#include <vector>

#include "metrics.h"

class Ssimulacra2 : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "SSIMULACRA2"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};
