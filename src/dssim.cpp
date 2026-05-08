// DSSIM: structural dissimilarity in CIE L*a*b*, multi-scale.
//
// Clean-room implementation from published references (Wang et al. 2003/2004
// for SSIM and MS-SSIM, the CIE 15.3 colorimetry standard, and Lesinski's
// public algorithm description for the DSSIM mapping). No code derived from
// the AGPL kornelski/dssim source.
//
// 1. sRGB(uint8) -> linear sRGB -> CIE XYZ (D65) -> CIE L*a*b*
// 2. Per octave (5 of them: 1, 1/2, 1/4, 1/8, 1/16):
//      SSIM with 11-tap Gaussian (sigma=1.5) on each of L, a, b
// 3. Aggregate: weighted sum across (channel x octave); L dominates,
//    octave weights from Wang 2003 MS-SSIM.
// 4. DSSIM = 1/SSIM - 1 (Lesinski convention; 0 = identical, lower is better,
//    no upper bound).
//
// Numbers won't match kornelski/dssim bit-for-bit (different internal tuning,
// e.g. weight choices and downsample filter), but the algorithmic shape and
// the score's monotonicity/range match.

#include "metrics.h"
#include "rgb24.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

inline float srgb_to_linear(uint8_t v) {
    const float x = v / 255.0f;
    return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
}

inline float lab_f(float t) {
    constexpr float delta = 6.0f / 29.0f;
    constexpr float delta3 = delta * delta * delta;
    if (t > delta3)
        return std::cbrt(t);
    return t / (3.0f * delta * delta) + 4.0f / 29.0f;
}

struct LabPlanes {
    int w = 0, h = 0;
    std::vector<float> L, a, b;
};

LabPlanes rgb_to_lab(const Rgb24& img) {
    constexpr float Xn = 0.95047f, Yn = 1.0f, Zn = 1.08883f;  // D65 white point
    const size_t n = static_cast<size_t>(img.width) * img.height;

    LabPlanes out;
    out.w = img.width;
    out.h = img.height;
    out.L.resize(n);
    out.a.resize(n);
    out.b.resize(n);

    for (size_t i = 0; i < n; ++i) {
        const float r = srgb_to_linear(img.pixels[i * 3 + 0]);
        const float g = srgb_to_linear(img.pixels[i * 3 + 1]);
        const float b = srgb_to_linear(img.pixels[i * 3 + 2]);
        const float X = 0.4124564f * r + 0.3575761f * g + 0.1804375f * b;
        const float Y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * b;
        const float Z = 0.0193339f * r + 0.1191920f * g + 0.9503041f * b;
        const float fx = lab_f(X / Xn);
        const float fy = lab_f(Y / Yn);
        const float fz = lab_f(Z / Zn);
        out.L[i] = 116.0f * fy - 16.0f;
        out.a[i] = 500.0f * (fx - fy);
        out.b[i] = 200.0f * (fy - fz);
    }
    return out;
}

inline int reflect(int x, int n) {
    if (x < 0)
        return -x;
    if (x >= n)
        return 2 * n - 2 - x;
    return x;
}

// 11-tap Gaussian, sigma=1.5 — the SSIM reference window.
constexpr float kGauss[11] = {0.001028f, 0.007595f, 0.036085f, 0.109634f, 0.213825f, 0.245666f,
                              0.213825f, 0.109634f, 0.036085f, 0.007595f, 0.001028f};

void blur_h(const std::vector<float>& src, std::vector<float>& dst, int w, int h) {
    constexpr int r = 5;
    dst.resize(src.size());
    for (int y = 0; y < h; ++y) {
        const float* row = &src[static_cast<size_t>(y) * w];
        float* out = &dst[static_cast<size_t>(y) * w];
        for (int x = 0; x < w; ++x) {
            float s = 0.0f;
            for (int k = -r; k <= r; ++k)
                s += row[reflect(x + k, w)] * kGauss[k + r];
            out[x] = s;
        }
    }
}

void blur_v(const std::vector<float>& src, std::vector<float>& dst, int w, int h) {
    constexpr int r = 5;
    dst.resize(src.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float s = 0.0f;
            for (int k = -r; k <= r; ++k)
                s += src[static_cast<size_t>(reflect(y + k, h)) * w + x] * kGauss[k + r];
            dst[static_cast<size_t>(y) * w + x] = s;
        }
    }
}

void blur(const std::vector<float>& src, std::vector<float>& dst, int w, int h) {
    std::vector<float> tmp(src.size());
    blur_h(src, tmp, w, h);
    blur_v(tmp, dst, w, h);
}

// Mean SSIM between two same-size planes. dyn_range scales the SSIM stability
// constants C1, C2 to the channel's expected value range.
float ssim_plane(const std::vector<float>& a, const std::vector<float>& b, int w, int h, float dyn_range) {
    constexpr float K1 = 0.01f, K2 = 0.03f;
    const float C1 = (K1 * dyn_range) * (K1 * dyn_range);
    const float C2 = (K2 * dyn_range) * (K2 * dyn_range);

    const size_t n = a.size();
    std::vector<float> a_sq(n), b_sq(n), ab(n);
    for (size_t i = 0; i < n; ++i) {
        a_sq[i] = a[i] * a[i];
        b_sq[i] = b[i] * b[i];
        ab[i] = a[i] * b[i];
    }

    std::vector<float> mu_a(n), mu_b(n), m_a_sq(n), m_b_sq(n), m_ab(n);
    blur(a, mu_a, w, h);
    blur(b, mu_b, w, h);
    blur(a_sq, m_a_sq, w, h);
    blur(b_sq, m_b_sq, w, h);
    blur(ab, m_ab, w, h);

    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const float mu_aa = mu_a[i] * mu_a[i];
        const float mu_bb = mu_b[i] * mu_b[i];
        const float mu_ab_i = mu_a[i] * mu_b[i];
        const float var_a = m_a_sq[i] - mu_aa;
        const float var_b = m_b_sq[i] - mu_bb;
        const float cov_ab = m_ab[i] - mu_ab_i;

        const float num = (2.0f * mu_ab_i + C1) * (2.0f * cov_ab + C2);
        const float den = (mu_aa + mu_bb + C1) * (var_a + var_b + C2);
        sum += num / den;
    }
    return static_cast<float>(sum / static_cast<double>(n));
}

std::vector<float> downsample_2x(const std::vector<float>& src, int w, int h, int& out_w, int& out_h) {
    out_w = w / 2;
    out_h = h / 2;
    std::vector<float> dst(static_cast<size_t>(out_w) * out_h);
    for (int y = 0; y < out_h; ++y) {
        for (int x = 0; x < out_w; ++x) {
            const int sy = y * 2, sx = x * 2;
            const float v = src[static_cast<size_t>(sy) * w + sx] + src[static_cast<size_t>(sy) * w + sx + 1] +
                            src[static_cast<size_t>(sy + 1) * w + sx] + src[static_cast<size_t>(sy + 1) * w + sx + 1];
            dst[static_cast<size_t>(y) * out_w + x] = v * 0.25f;
        }
    }
    return dst;
}

}  // namespace

std::optional<std::vector<Score>> Dssim::measure(const Image& ref, const Image& dist) noexcept {
    auto a_rgb = load_rgb24(ref);
    auto b_rgb = load_rgb24(dist);
    if (!a_rgb || !b_rgb)
        return std::nullopt;
    if (a_rgb->width != b_rgb->width || a_rgb->height != b_rgb->height)
        return std::nullopt;

    const LabPlanes A = rgb_to_lab(*a_rgb);
    const LabPlanes B = rgb_to_lab(*b_rgb);

    // L* spans 0..100, a*/b* roughly -128..128.
    constexpr float chan_range[3] = {100.0f, 256.0f, 256.0f};
    constexpr float chan_w[3] = {0.8f, 0.1f, 0.1f};                                          // luminance dominates
    constexpr float scale_w[5] = {0.0448f, 0.2856f, 0.3001f, 0.2363f, 0.1333f};              // Wang 2003

    std::vector<float> ds_a[3], ds_b[3];
    const std::vector<float>* planes_a[3] = {&A.L, &A.a, &A.b};
    const std::vector<float>* planes_b[3] = {&B.L, &B.a, &B.b};
    int w = A.w, h = A.h;

    double total_w = 0.0;
    double weighted = 0.0;

    for (int s = 0; s < 5; ++s) {
        if (w < 16 || h < 16)
            break;
        for (int c = 0; c < 3; ++c) {
            const float ssim = ssim_plane(*planes_a[c], *planes_b[c], w, h, chan_range[c]);
            const double weight = static_cast<double>(scale_w[s]) * chan_w[c];
            weighted += weight * ssim;
            total_w += weight;
        }
        if (s < 4) {
            int nw = 0, nh = 0;
            for (int c = 0; c < 3; ++c) {
                ds_a[c] = downsample_2x(*planes_a[c], w, h, nw, nh);
                ds_b[c] = downsample_2x(*planes_b[c], w, h, nw, nh);
                planes_a[c] = &ds_a[c];
                planes_b[c] = &ds_b[c];
            }
            w = nw;
            h = nh;
        }
    }

    if (total_w <= 0.0)
        return std::nullopt;

    const double ssim_mean = weighted / total_w;
    if (ssim_mean <= 0.0)
        return std::nullopt;

    double dssim = 1.0 / ssim_mean - 1.0;
    if (dssim < 0.0)
        dssim = 0.0;

    return std::vector<Score>{{"DSSIM", static_cast<float>(dssim)}};
}
