#pragma once

#include <optional>
#include <vector>

#include "metrics.h"

class Fsim : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "FSIM"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};

class FsimC : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "FSIMc"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};
