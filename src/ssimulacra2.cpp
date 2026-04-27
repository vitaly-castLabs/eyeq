#include "metrics.h"

#include <algorithm>

#include <ssimulacra2.h>

#include "lib/extras/codec.h"

std::optional<std::vector<Score>> Ssimulacra2::measure(const Image& ref, const Image& dist) noexcept {
    if (ref.path.empty() || dist.path.empty())
        return std::nullopt;

    jxl::CodecInOut ref_io;
    jxl::CodecInOut dist_io;
    if (!jxl::SetFromFile(ref.path, jxl::extras::ColorHints(), &ref_io))
        return std::nullopt;
    if (ref_io.xsize() < 8 || ref_io.ysize() < 8)
        return std::nullopt;
    if (!jxl::SetFromFile(dist.path, jxl::extras::ColorHints(), &dist_io))
        return std::nullopt;
    if (ref_io.xsize() != dist_io.xsize() || ref_io.ysize() != dist_io.ysize())
        return std::nullopt;

    const double score = ref_io.Main().HasAlpha() ? std::min(ComputeSSIMULACRA2(ref_io.Main(), dist_io.Main(), 0.1f).Score(),
                                                             ComputeSSIMULACRA2(ref_io.Main(), dist_io.Main(), 0.9f).Score())
                                                  : ComputeSSIMULACRA2(ref_io.Main(), dist_io.Main()).Score();
    return std::vector<Score>{{"SSIMULACRA2", static_cast<float>(score)}};
}
