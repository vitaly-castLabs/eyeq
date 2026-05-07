#pragma once

#include <optional>
#include <vector>

#include "metrics.h"

class XpsnrBase : public Metrics {
public:
    using Metrics::Metrics;

protected:
    struct Planes {
        float y, u, v;
    };

    std::optional<Planes> compute(const Image& ref, const Image& dist) noexcept;
};

class Xpsnr : public XpsnrBase {
public:
    using XpsnrBase::XpsnrBase;

    const char* name() const override { return "XPSNR"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};

class XpsnrY : public XpsnrBase {
public:
    using XpsnrBase::XpsnrBase;

    const char* name() const override { return "XPSNR (Y)"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override;
};
