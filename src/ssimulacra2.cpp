#include "metrics.h"
#include "rgb24.h"

#include <algorithm>

#include <ssimulacra2.h>

#include "lib/extras/codec.h"

std::optional<std::vector<Score>> Ssimulacra2::measure(const Image& ref, const Image& dist) noexcept {
    if (ref.path.empty() || dist.path.empty())
        return std::nullopt;

    // If either input is raw YUV, force the other through the same I420->PPM
    // round-trip so both sides have identical chroma resolution. Otherwise
    // SSIMULACRA2 would compare full-RGB PNG against chroma-subsampled YUV
    // and report a low score for what is effectively a 4:2:0 identity check.
    const PathMode mode = (is_raw_yuv_path(ref.path) || is_raw_yuv_path(dist.path)) ? PathMode::ForcePpm : PathMode::Auto;
    auto ref_holder = resolve_path(ref, mode);
    auto dist_holder = resolve_path(dist, mode);
    if (!ref_holder || !dist_holder)
        return std::nullopt;

    jxl::CodecInOut ref_io;
    jxl::CodecInOut dist_io;
    if (!jxl::SetFromFile(ref_holder->path(), jxl::extras::ColorHints(), &ref_io))
        return std::nullopt;
    if (ref_io.xsize() < 8 || ref_io.ysize() < 8)
        return std::nullopt;
    if (!jxl::SetFromFile(dist_holder->path(), jxl::extras::ColorHints(), &dist_io))
        return std::nullopt;
    if (ref_io.xsize() != dist_io.xsize() || ref_io.ysize() != dist_io.ysize())
        return std::nullopt;

    const double score = ref_io.Main().HasAlpha() ? std::min(ComputeSSIMULACRA2(ref_io.Main(), dist_io.Main(), 0.1f).Score(),
                                                             ComputeSSIMULACRA2(ref_io.Main(), dist_io.Main(), 0.9f).Score())
                                                  : ComputeSSIMULACRA2(ref_io.Main(), dist_io.Main()).Score();
    return std::vector<Score>{{"SSIMULACRA2", static_cast<float>(score)}};
}
