#pragma once

#include <optional>
#include <vector>

#include "metrics.h"

class Mdsi : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "MDSI"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};
