// Highway-accelerated pieces for the native SSIMULACRA2 port.
//
// This is a narrow extraction inspired by JPEG XL's gauss_blur.cc vertical
// recursive Gaussian pass. It keeps only the data-parallel scan needed by eyeq.

#include "src/ssimulacra2_hwy.h"

#include <algorithm>
#include <cstddef>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/ssimulacra2_hwy.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace eyeq_ssimulacra2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

void FastGaussian1DHwy(const Ssimulacra2RecursiveGaussian& rg, const float* HWY_RESTRICT in, int width, float* HWY_RESTRICT out) {
    const hn::CappedTag<float, 4> d;
    const size_t lanes = hn::Lanes(d);

    const auto mul_in_0 = hn::LoadU(d, rg.mul_in + 0 * 4);
    const auto mul_in_1 = hn::LoadU(d, rg.mul_in + 1 * 4);
    const auto mul_in_2 = hn::LoadU(d, rg.mul_in + 2 * 4);
    const auto mul_prev_0 = hn::LoadU(d, rg.mul_prev + 0 * 4);
    const auto mul_prev_1 = hn::LoadU(d, rg.mul_prev + 1 * 4);
    const auto mul_prev_2 = hn::LoadU(d, rg.mul_prev + 2 * 4);
    const auto mul_prev2_0 = hn::LoadU(d, rg.mul_prev2 + 0 * 4);
    const auto mul_prev2_1 = hn::LoadU(d, rg.mul_prev2 + 1 * 4);
    const auto mul_prev2_2 = hn::LoadU(d, rg.mul_prev2 + 2 * 4);

    auto prev_0 = hn::Zero(d);
    auto prev_1 = hn::Zero(d);
    auto prev_2 = hn::Zero(d);
    auto prev2_0 = hn::Zero(d);
    auto prev2_1 = hn::Zero(d);
    auto prev2_2 = hn::Zero(d);

    int n = -rg.radius + 1;
    const int first_aligned = static_cast<int>(hwy::RoundUpTo(static_cast<size_t>(rg.radius + 1), lanes));
    for (; n < std::min(first_aligned, width); ++n) {
        const int left = n - rg.radius - 1;
        const int right = n + rg.radius - 1;
        const float left_val = left >= 0 ? in[left] : 0.0f;
        const float right_val = right < width ? in[right] : 0.0f;
        const auto sum = hn::Set(d, left_val + right_val);

        auto cur_0 = hn::Mul(sum, mul_in_0);
        auto cur_1 = hn::Mul(sum, mul_in_1);
        auto cur_2 = hn::Mul(sum, mul_in_2);
        cur_0 = hn::MulAdd(mul_prev2_0, prev2_0, cur_0);
        cur_1 = hn::MulAdd(mul_prev2_1, prev2_1, cur_1);
        cur_2 = hn::MulAdd(mul_prev2_2, prev2_2, cur_2);
        prev2_0 = prev_0;
        prev2_1 = prev_1;
        prev2_2 = prev_2;
        cur_0 = hn::MulAdd(mul_prev_0, prev_0, cur_0);
        cur_1 = hn::MulAdd(mul_prev_1, prev_1, cur_1);
        cur_2 = hn::MulAdd(mul_prev_2, prev_2, cur_2);
        prev_0 = cur_0;
        prev_1 = cur_1;
        prev_2 = cur_2;

        if (n >= 0)
            out[n] = hn::GetLane(hn::Add(cur_0, hn::Add(cur_1, cur_2)));
    }

#if HWY_TARGET != HWY_SCALAR
    prev2_0 = hn::Broadcast<0>(prev2_0);
    prev2_1 = hn::Broadcast<0>(prev2_1);
    prev2_2 = hn::Broadcast<0>(prev2_2);
    prev_0 = hn::Broadcast<0>(prev_0);
    prev_1 = hn::Broadcast<0>(prev_1);
    prev_2 = hn::Broadcast<0>(prev_2);
#endif

    for (; n < width - rg.radius + 1 - (static_cast<int>(lanes) - 1); n += static_cast<int>(lanes)) {
        const auto sum = hn::Add(hn::LoadU(d, in + n - rg.radius - 1), hn::LoadU(d, in + n + rg.radius - 1));
        const auto in0 = hn::Broadcast<0>(sum);

        auto cur_0 = hn::Mul(in0, mul_in_0);
        auto cur_1 = hn::Mul(in0, mul_in_1);
        auto cur_2 = hn::Mul(in0, mul_in_2);

#if HWY_TARGET != HWY_SCALAR
        if constexpr (4 >= 2) {
            const auto in1 = hn::Broadcast<1>(sum);
            cur_0 = hn::MulAdd(hn::ShiftLeftLanes<1>(mul_in_0), in1, cur_0);
            cur_1 = hn::MulAdd(hn::ShiftLeftLanes<1>(mul_in_1), in1, cur_1);
            cur_2 = hn::MulAdd(hn::ShiftLeftLanes<1>(mul_in_2), in1, cur_2);
        }
        if constexpr (4 >= 4) {
            const auto in2 = hn::Broadcast<2>(sum);
            cur_0 = hn::MulAdd(hn::ShiftLeftLanes<2>(mul_in_0), in2, cur_0);
            cur_1 = hn::MulAdd(hn::ShiftLeftLanes<2>(mul_in_1), in2, cur_1);
            cur_2 = hn::MulAdd(hn::ShiftLeftLanes<2>(mul_in_2), in2, cur_2);

            const auto in3 = hn::Broadcast<3>(sum);
            cur_0 = hn::MulAdd(hn::ShiftLeftLanes<3>(mul_in_0), in3, cur_0);
            cur_1 = hn::MulAdd(hn::ShiftLeftLanes<3>(mul_in_1), in3, cur_1);
            cur_2 = hn::MulAdd(hn::ShiftLeftLanes<3>(mul_in_2), in3, cur_2);
        }
#endif

        cur_0 = hn::MulAdd(mul_prev2_0, prev2_0, cur_0);
        cur_1 = hn::MulAdd(mul_prev2_1, prev2_1, cur_1);
        cur_2 = hn::MulAdd(mul_prev2_2, prev2_2, cur_2);
        cur_0 = hn::MulAdd(mul_prev_0, prev_0, cur_0);
        cur_1 = hn::MulAdd(mul_prev_1, prev_1, cur_1);
        cur_2 = hn::MulAdd(mul_prev_2, prev_2, cur_2);

#if HWY_TARGET == HWY_SCALAR
        prev2_0 = prev_0;
        prev2_1 = prev_1;
        prev2_2 = prev_2;
        prev_0 = cur_0;
        prev_1 = cur_1;
        prev_2 = cur_2;
#else
        prev2_0 = hn::Broadcast<2>(cur_0);
        prev2_1 = hn::Broadcast<2>(cur_1);
        prev2_2 = hn::Broadcast<2>(cur_2);
        prev_0 = hn::Broadcast<3>(cur_0);
        prev_1 = hn::Broadcast<3>(cur_1);
        prev_2 = hn::Broadcast<3>(cur_2);
#endif

        hn::Store(hn::Add(cur_0, hn::Add(cur_1, cur_2)), d, out + n);
    }

    for (; n < width; ++n) {
        const int left = n - rg.radius - 1;
        const int right = n + rg.radius - 1;
        const float left_val = left >= 0 ? in[left] : 0.0f;
        const float right_val = right < width ? in[right] : 0.0f;
        const auto sum = hn::Set(d, left_val + right_val);

        auto cur_0 = hn::Mul(sum, mul_in_0);
        auto cur_1 = hn::Mul(sum, mul_in_1);
        auto cur_2 = hn::Mul(sum, mul_in_2);
        cur_0 = hn::MulAdd(mul_prev2_0, prev2_0, cur_0);
        cur_1 = hn::MulAdd(mul_prev2_1, prev2_1, cur_1);
        cur_2 = hn::MulAdd(mul_prev2_2, prev2_2, cur_2);
        prev2_0 = prev_0;
        prev2_1 = prev_1;
        prev2_2 = prev_2;
        cur_0 = hn::MulAdd(mul_prev_0, prev_0, cur_0);
        cur_1 = hn::MulAdd(mul_prev_1, prev_1, cur_1);
        cur_2 = hn::MulAdd(mul_prev_2, prev_2, cur_2);
        prev_0 = cur_0;
        prev_1 = cur_1;
        prev_2 = cur_2;

        out[n] = hn::GetLane(hn::Add(cur_0, hn::Add(cur_1, cur_2)));
    }
}

void FastGaussianHorizontalHwy(const Ssimulacra2RecursiveGaussian& rg, const float* HWY_RESTRICT in, int w, int h, int stride, float* HWY_RESTRICT out) {
    for (int y = 0; y < h; ++y)
        FastGaussian1DHwy(rg, in + static_cast<size_t>(y) * stride, w, out + static_cast<size_t>(y) * stride);
}

void FastGaussianVerticalHwy(const Ssimulacra2RecursiveGaussian& rg, const float* HWY_RESTRICT in, int w, int h, int stride, float* HWY_RESTRICT out) {
    const hn::ScalableTag<float> d;
    const size_t max_lanes = hn::Lanes(d);

    const auto n2_0 = hn::Set(d, rg.n2[0]);
    const auto n2_1 = hn::Set(d, rg.n2[1]);
    const auto n2_2 = hn::Set(d, rg.n2[2]);
    const auto neg_d1_0 = hn::Set(d, -rg.d1[0]);
    const auto neg_d1_1 = hn::Set(d, -rg.d1[1]);
    const auto neg_d1_2 = hn::Set(d, -rg.d1[2]);
    const auto neg_one = hn::Set(d, -1.0f);
    const auto zero = hn::Zero(d);

    for (int x = 0; x < w; x += static_cast<int>(max_lanes)) {
        const size_t lanes = std::min(max_lanes, static_cast<size_t>(w - x));
        auto prev0 = zero;
        auto prev1 = zero;
        auto prev2 = zero;
        auto prev20 = zero;
        auto prev21 = zero;
        auto prev22 = zero;

        for (int n = -rg.radius + 1; n < h; ++n) {
            const int top = n - rg.radius - 1;
            const int bottom = n + rg.radius - 1;

            auto sum = zero;
            if (top >= 0) {
                const float* top_ptr = in + static_cast<size_t>(top) * stride + x;
                sum = lanes == max_lanes ? hn::Load(d, top_ptr) : hn::LoadN(d, top_ptr, lanes);
            }
            if (bottom < h) {
                const float* bottom_ptr = in + static_cast<size_t>(bottom) * stride + x;
                sum = hn::Add(sum, lanes == max_lanes ? hn::Load(d, bottom_ptr) : hn::LoadN(d, bottom_ptr, lanes));
            }

            auto cur0 = hn::Mul(n2_0, sum);
            cur0 = hn::MulAdd(neg_one, prev20, cur0);
            cur0 = hn::MulAdd(neg_d1_0, prev0, cur0);
            auto cur1 = hn::Mul(n2_1, sum);
            cur1 = hn::MulAdd(neg_one, prev21, cur1);
            cur1 = hn::MulAdd(neg_d1_1, prev1, cur1);
            auto cur2 = hn::Mul(n2_2, sum);
            cur2 = hn::MulAdd(neg_one, prev22, cur2);
            cur2 = hn::MulAdd(neg_d1_2, prev2, cur2);

            prev20 = prev0;
            prev21 = prev1;
            prev22 = prev2;
            prev0 = cur0;
            prev1 = cur1;
            prev2 = cur2;

            if (n >= 0) {
                const auto value = hn::Add(cur0, hn::Add(cur1, cur2));
                float* out_ptr = out + static_cast<size_t>(n) * stride + x;
                if (lanes == max_lanes)
                    hn::Store(value, d, out_ptr);
                else
                    hn::StoreN(value, d, out_ptr, lanes);
            }
        }
    }
}

} // namespace HWY_NAMESPACE
} // namespace eyeq_ssimulacra2
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace eyeq_ssimulacra2 {
HWY_EXPORT(FastGaussianHorizontalHwy);
HWY_EXPORT(FastGaussianVerticalHwy);
} // namespace eyeq_ssimulacra2

void ssimulacra2_fast_gaussian_horizontal_hwy(const Ssimulacra2RecursiveGaussian& rg, const float* in, int w, int h, int stride, float* out) {
    HWY_DYNAMIC_DISPATCH(eyeq_ssimulacra2::FastGaussianHorizontalHwy)(rg, in, w, h, stride, out);
}

void ssimulacra2_fast_gaussian_vertical_hwy(const Ssimulacra2RecursiveGaussian& rg, const float* in, int w, int h, int stride, float* out) {
    HWY_DYNAMIC_DISPATCH(eyeq_ssimulacra2::FastGaussianVerticalHwy)(rg, in, w, h, stride, out);
}
#endif
