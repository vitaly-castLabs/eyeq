// SSIMULACRA 2.1 metric.
//
// The metric structure, XYB constants, recursive Gaussian parameters, and
// scoring weights are derived from Cloudinary/JPEG XL SSIMULACRA2:
// Copyright (c) Cloudinary. All rights reserved.
// Copyright (c) the JPEG XL Project Authors. All rights reserved.
// Use is governed by the BSD-style licenses in the upstream projects.
//
// This file intentionally keeps only the metric math needed by eyeq. It depends
// on Highway for SIMD blur dispatch, but not on libjxl, lcms, libpng, or
// temporary image files.

#include "metrics.h"

#include "rgb24.h"
#include "ssimulacra2_hwy.h"

#include <hwy/aligned_allocator.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <thread>
#include <vector>

namespace {

constexpr float kC2 = 0.0009f;
constexpr int kNumScales = 6;
constexpr double kPi = 3.14159265358979323846;
constexpr int kStrideAlign = 16;

int padded_stride(int width) {
    return ((width + kStrideAlign - 1) / kStrideAlign) * kStrideAlign;
}

class PlaneF {
public:
    PlaneF() = default;
    PlaneF(const PlaneF&) = delete;
    PlaneF& operator=(const PlaneF&) = delete;
    PlaneF(PlaneF&&) noexcept = default;
    PlaneF& operator=(PlaneF&&) noexcept = default;

    void resize(int width, int height) {
        w_ = width;
        h_ = height;
        stride_ = padded_stride(width);
        const size_t needed = static_cast<size_t>(stride_) * h_;
        if (needed > capacity_) {
            data_ = hwy::AllocateAligned<float>(needed);
            if (!data_)
                throw std::bad_alloc();
            capacity_ = needed;
        }
    }

    float* data() { return data_.get(); }
    const float* data() const { return data_.get(); }
    float* row(int y) { return data_.get() + static_cast<size_t>(y) * stride_; }
    const float* row(int y) const { return data_.get() + static_cast<size_t>(y) * stride_; }
    int stride() const { return stride_; }

private:
    int w_ = 0;
    int h_ = 0;
    int stride_ = 0;
    size_t capacity_ = 0;
    hwy::AlignedFreeUniquePtr<float[]> data_;
};

struct Image3F {
    int w = 0;
    int h = 0;
    std::array<PlaneF, 3> p;

    Image3F() = default;
    Image3F(const Image3F&) = delete;
    Image3F& operator=(const Image3F&) = delete;
    Image3F(Image3F&&) noexcept = default;
    Image3F& operator=(Image3F&&) noexcept = default;
    Image3F(int width, int height) { resize(width, height); }

    void resize(int width, int height) {
        w = width;
        h = height;
        for (auto& plane: p)
            plane.resize(w, h);
    }

    float* row(int c, int y) { return p[c].row(y); }
    const float* row(int c, int y) const { return p[c].row(y); }
};

struct ScaleScores {
    double avg_ssim[3 * 2] = {};
    double avg_edgediff[3 * 4] = {};
};

struct Msssim {
    std::vector<ScaleScores> scales;
};

double fourth(double x) {
    x *= x;
    return x * x;
}

bool invert_3x3(double m[9]) {
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < std::numeric_limits<double>::epsilon())
        return false;

    double inv[9];
    inv[0] = (m[4] * m[8] - m[5] * m[7]) / det;
    inv[1] = (m[2] * m[7] - m[1] * m[8]) / det;
    inv[2] = (m[1] * m[5] - m[2] * m[4]) / det;
    inv[3] = (m[5] * m[6] - m[3] * m[8]) / det;
    inv[4] = (m[0] * m[8] - m[2] * m[6]) / det;
    inv[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    inv[6] = (m[3] * m[7] - m[4] * m[6]) / det;
    inv[7] = (m[1] * m[6] - m[0] * m[7]) / det;
    inv[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    std::copy(std::begin(inv), std::end(inv), m);
    return true;
}

Ssimulacra2RecursiveGaussian create_recursive_gaussian(double sigma) {
    Ssimulacra2RecursiveGaussian rg;
    const double radius = std::round(3.2795 * sigma + 0.2546);
    const double pi_div_2r = kPi / (2.0 * radius);
    const double omega[3] = {pi_div_2r, 3.0 * pi_div_2r, 5.0 * pi_div_2r};

    const double p1 = +1.0 / std::tan(0.5 * omega[0]);
    const double p3 = -1.0 / std::tan(0.5 * omega[1]);
    const double p5 = +1.0 / std::tan(0.5 * omega[2]);
    const double r1 = +p1 * p1 / std::sin(omega[0]);
    const double r3 = -p3 * p3 / std::sin(omega[1]);
    const double r5 = +p5 * p5 / std::sin(omega[2]);

    const double neg_half_sigma2 = -0.5 * sigma * sigma;
    const double recip_radius = 1.0 / radius;
    double rho[3];
    for (int i = 0; i < 3; ++i)
        rho[i] = std::exp(neg_half_sigma2 * omega[i] * omega[i]) * recip_radius;

    const double d13 = p1 * r3 - r1 * p3;
    const double d35 = p3 * r5 - r3 * p5;
    const double d51 = p5 * r1 - r5 * p1;
    const double zeta15 = d35 / d13;
    const double zeta35 = d51 / d13;

    double a[9] = {p1, p3, p5, r1, r3, r5, zeta15, zeta35, 1.0};
    if (!invert_3x3(a))
        return rg;

    const double gamma[3] = {1.0, radius * radius - sigma * sigma, zeta15 * rho[0] + zeta35 * rho[1] + rho[2]};
    double beta[3] = {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            beta[row] += a[row * 3 + col] * gamma[col];
    }

    rg.radius = static_cast<int>(radius);
    for (int i = 0; i < 3; ++i) {
        const double n2 = -beta[i] * std::cos(omega[i] * (radius + 1.0));
        const double d1 = -2.0 * std::cos(omega[i]);
        const double d2 = d1 * d1;
        rg.n2[i] = static_cast<float>(n2);
        rg.d1[i] = static_cast<float>(d1);
        rg.mul_prev[4 * i + 0] = static_cast<float>(-d1);
        rg.mul_prev[4 * i + 1] = static_cast<float>(d2 - 1.0);
        rg.mul_prev[4 * i + 2] = static_cast<float>(-d2 * d1 + 2.0 * d1);
        rg.mul_prev[4 * i + 3] = static_cast<float>(d2 * d2 - 3.0 * d2 + 1.0);
        rg.mul_prev2[4 * i + 0] = -1.0f;
        rg.mul_prev2[4 * i + 1] = static_cast<float>(d1);
        rg.mul_prev2[4 * i + 2] = static_cast<float>(-d2 + 1.0);
        rg.mul_prev2[4 * i + 3] = static_cast<float>(d2 * d1 - 2.0 * d1);
        rg.mul_in[4 * i + 0] = static_cast<float>(n2);
        rg.mul_in[4 * i + 1] = static_cast<float>(-d1 * n2);
        rg.mul_in[4 * i + 2] = static_cast<float>(d2 * n2 - n2);
        rg.mul_in[4 * i + 3] = static_cast<float>(-d2 * d1 * n2 + 2.0 * d1 * n2);
    }
    return rg;
}

struct BlurScratch {
    std::array<PlaneF, 3> temp;

    void resize(int w, int h) {
        for (auto& plane: temp)
            plane.resize(w, h);
    }
};

void gaussian_blur_plane(const Ssimulacra2RecursiveGaussian& rg, const PlaneF& in, int w, int h, PlaneF& temp, PlaneF& out) {
    temp.resize(w, h);
    out.resize(w, h);

    ssimulacra2_fast_gaussian_horizontal_hwy(rg, in.data(), w, h, in.stride(), temp.data());
    ssimulacra2_fast_gaussian_vertical_hwy(rg, temp.data(), w, h, temp.stride(), out.data());
}

void gaussian_blur_to(const Ssimulacra2RecursiveGaussian& rg, const Image3F& in, BlurScratch& scratch, Image3F& out) {
    out.w = in.w;
    out.h = in.h;
    const size_t pixels = static_cast<size_t>(in.w) * in.h;
    scratch.resize(in.w, in.h);
    for (auto& plane: out.p)
        plane.resize(in.w, in.h);

    if (pixels >= 256 * 256) {
        std::thread workers[3];
        for (int c = 0; c < 3; ++c) {
            workers[c] = std::thread([&, c] { gaussian_blur_plane(rg, in.p[c], in.w, in.h, scratch.temp[c], out.p[c]); });
        }
        for (auto& worker: workers)
            worker.join();
    } else {
        for (int c = 0; c < 3; ++c)
            gaussian_blur_plane(rg, in.p[c], in.w, in.h, scratch.temp[c], out.p[c]);
    }
}

Image3F downsample_2x(const Image3F& in) {
    const int out_w = (in.w + 1) / 2;
    const int out_h = (in.h + 1) / 2;
    Image3F out(out_w, out_h);
    for (int c = 0; c < 3; ++c) {
        for (int oy = 0; oy < out_h; ++oy) {
            float* dst = out.row(c, oy);
            for (int ox = 0; ox < out_w; ++ox) {
                float sum = 0.0f;
                for (int iy = 0; iy < 2; ++iy) {
                    for (int ix = 0; ix < 2; ++ix) {
                        const int x = std::min(ox * 2 + ix, in.w - 1);
                        const int y = std::min(oy * 2 + iy, in.h - 1);
                        sum += in.row(c, y)[x];
                    }
                }
                dst[ox] = sum * 0.25f;
            }
        }
    }
    return out;
}

float srgb_to_linear(uint8_t v) {
    const float e = static_cast<float>(v) / 255.0f;
    if (e <= 0.04045f)
        return e / 12.92f;
    constexpr float p[5] = {2.200248328e-04f, 1.043637593e-02f, 1.624820318e-01f, 7.961564959e-01f, 8.210152774e-01f};
    constexpr float q[5] = {2.631846970e-01f, 1.076976492e+00f, 4.987528350e-01f, -5.512498495e-02f, 6.521209011e-03f};
    const float numerator = (((p[4] * e + p[3]) * e + p[2]) * e + p[1]) * e + p[0];
    const float denominator = (((q[4] * e + q[3]) * e + q[2]) * e + q[1]) * e + q[0];
    return numerator / denominator;
}

Image3F rgb_to_linear(const Rgb24& img) {
    Image3F out(img.width, img.height);
    for (int y = 0; y < img.height; ++y) {
        float* r = out.row(0, y);
        float* g = out.row(1, y);
        float* b = out.row(2, y);
        const uint8_t* src = img.pixels.data() + static_cast<size_t>(y) * img.width * 3;
        for (int x = 0; x < img.width; ++x) {
            r[x] = srgb_to_linear(src[x * 3 + 0]);
            g[x] = srgb_to_linear(src[x * 3 + 1]);
            b[x] = srgb_to_linear(src[x * 3 + 2]);
        }
    }
    return out;
}

float cube_root_and_add(float x, float add) {
    constexpr int32_t kExpBias = 0x54800000;
    constexpr int32_t kExpMul = 0x002AAAAA;
    constexpr float k1_3 = 1.0f / 3.0f;
    constexpr float k4_3 = 4.0f / 3.0f;

    const int32_t bits = std::bit_cast<int32_t>(x);
    const int32_t approx_bits = bits == 0 ? 0 : kExpBias - ((bits >> 23) * kExpMul);
    float r = std::bit_cast<float>(approx_bits);

    const float xa_3 = k1_3 * x;
    for (int i = 0; i < 3; ++i) {
        const float r2 = r * r;
        r = k4_3 * r - xa_3 * (r2 * r2);
    }
    float r2 = r * r;
    r = k1_3 * (r - x * (r2 * r2)) + r;
    r2 = r * r;
    return r2 * x + add;
}

void linear_to_positive_xyb_to(const Image3F& linear, Image3F& out) {
    constexpr float kM[9] = {0.30f, 0.622f, 0.078f, 0.23f, 0.692f, 0.078f, 0.24342268924547819f, 0.20476744424496821f, 0.5518098665095536f};
    constexpr float kBias = 0.0037930732552754493f;
    const float neg_bias_cbrt = -std::cbrt(kBias);

    out.resize(linear.w, linear.h);
    for (int y = 0; y < linear.h; ++y) {
        const float* r = linear.row(0, y);
        const float* g = linear.row(1, y);
        const float* b = linear.row(2, y);
        float* xyb_x = out.row(0, y);
        float* xyb_y = out.row(1, y);
        float* xyb_b = out.row(2, y);
        for (int x = 0; x < linear.w; ++x) {
            const float mixed0 = std::max(0.0f, kM[0] * r[x] + kM[1] * g[x] + kM[2] * b[x] + kBias);
            const float mixed1 = std::max(0.0f, kM[3] * r[x] + kM[4] * g[x] + kM[5] * b[x] + kBias);
            const float mixed2 = std::max(0.0f, kM[6] * r[x] + kM[7] * g[x] + kM[8] * b[x] + kBias);
            const float op0 = cube_root_and_add(mixed0, neg_bias_cbrt);
            const float op1 = cube_root_and_add(mixed1, neg_bias_cbrt);
            const float op2 = cube_root_and_add(mixed2, neg_bias_cbrt);

            const float xyb0 = 0.5f * (op0 - op1);
            const float xyb1 = 0.5f * (op0 + op1);
            xyb_x[x] = xyb0 * 14.0f + 0.42f;
            xyb_y[x] = xyb1 + 0.01f;
            xyb_b[x] = (op2 - xyb1) + 0.55f;
        }
    }
}

void multiply(const Image3F& a, const Image3F& b, Image3F& out) {
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < a.h; ++y) {
            const float* row_a = a.row(c, y);
            const float* row_b = b.row(c, y);
            float* row_out = out.row(c, y);
            for (int x = 0; x < a.w; ++x)
                row_out[x] = row_a[x] * row_b[x];
        }
    }
}

void ssim_map(const Image3F& m1, const Image3F& m2, const Image3F& s11, const Image3F& s22, const Image3F& s12, double* plane_averages) {
    const double inv_pixels = 1.0 / (static_cast<double>(m1.w) * m1.h);
    for (int c = 0; c < 3; ++c) {
        double sum[2] = {};
        for (int y = 0; y < m1.h; ++y) {
            const float* row_m1 = m1.row(c, y);
            const float* row_m2 = m2.row(c, y);
            const float* row_s11 = s11.row(c, y);
            const float* row_s22 = s22.row(c, y);
            const float* row_s12 = s12.row(c, y);
            for (int x = 0; x < m1.w; ++x) {
                const float mu1 = row_m1[x];
                const float mu2 = row_m2[x];
                const float mu11 = mu1 * mu1;
                const float mu22 = mu2 * mu2;
                const float mu12 = mu1 * mu2;
                const float num_m = 1.0f - (mu1 - mu2) * (mu1 - mu2);
                const float num_s = 2.0f * (row_s12[x] - mu12) + kC2;
                const float denom_s = (row_s11[x] - mu11) + (row_s22[x] - mu22) + kC2;
                const double d = std::max(1.0 - (num_m * num_s / denom_s), 0.0);
                sum[0] += d;
                sum[1] += fourth(d);
            }
        }
        plane_averages[c * 2] = inv_pixels * sum[0];
        plane_averages[c * 2 + 1] = std::sqrt(std::sqrt(inv_pixels * sum[1]));
    }
}

void edge_diff_map(const Image3F& img1, const Image3F& mu1, const Image3F& img2, const Image3F& mu2, double* plane_averages) {
    const double inv_pixels = 1.0 / (static_cast<double>(img1.w) * img1.h);
    for (int c = 0; c < 3; ++c) {
        double sum[4] = {};
        for (int y = 0; y < img1.h; ++y) {
            const float* row1 = img1.row(c, y);
            const float* row2 = img2.row(c, y);
            const float* rowm1 = mu1.row(c, y);
            const float* rowm2 = mu2.row(c, y);
            for (int x = 0; x < img1.w; ++x) {
                const double d1 = (1.0 + std::abs(row2[x] - rowm2[x])) / (1.0 + std::abs(row1[x] - rowm1[x])) - 1.0;
                const double artifact = std::max(d1, 0.0);
                const double detail_lost = std::max(-d1, 0.0);
                sum[0] += artifact;
                sum[1] += fourth(artifact);
                sum[2] += detail_lost;
                sum[3] += fourth(detail_lost);
            }
        }
        plane_averages[c * 4] = inv_pixels * sum[0];
        plane_averages[c * 4 + 1] = std::sqrt(std::sqrt(inv_pixels * sum[1]));
        plane_averages[c * 4 + 2] = inv_pixels * sum[2];
        plane_averages[c * 4 + 3] = std::sqrt(std::sqrt(inv_pixels * sum[3]));
    }
}

Msssim compute_ssimulacra2(const Rgb24& ref_rgb, const Rgb24& dist_rgb) {
    Msssim msssim;
    Image3F ref_linear = rgb_to_linear(ref_rgb);
    Image3F dist_linear = rgb_to_linear(dist_rgb);
    const Ssimulacra2RecursiveGaussian rg = create_recursive_gaussian(1.5);
    BlurScratch blur_scratch;
    Image3F img1;
    Image3F img2;
    Image3F mul;
    Image3F sigma1_sq;
    Image3F sigma2_sq;
    Image3F sigma12;
    Image3F mu1;
    Image3F mu2;

    for (int scale = 0; scale < kNumScales; ++scale) {
        if (ref_linear.w < 8 || ref_linear.h < 8)
            break;
        if (scale != 0) {
            ref_linear = downsample_2x(ref_linear);
            dist_linear = downsample_2x(dist_linear);
        }

        linear_to_positive_xyb_to(ref_linear, img1);
        linear_to_positive_xyb_to(dist_linear, img2);
        mul.resize(img1.w, img1.h);
        sigma1_sq.resize(img1.w, img1.h);
        sigma2_sq.resize(img1.w, img1.h);
        sigma12.resize(img1.w, img1.h);
        mu1.resize(img1.w, img1.h);
        mu2.resize(img1.w, img1.h);

        multiply(img1, img1, mul);
        gaussian_blur_to(rg, mul, blur_scratch, sigma1_sq);
        multiply(img2, img2, mul);
        gaussian_blur_to(rg, mul, blur_scratch, sigma2_sq);
        multiply(img1, img2, mul);
        gaussian_blur_to(rg, mul, blur_scratch, sigma12);

        gaussian_blur_to(rg, img1, blur_scratch, mu1);
        gaussian_blur_to(rg, img2, blur_scratch, mu2);

        ScaleScores scores;
        ssim_map(mu1, mu2, sigma1_sq, sigma2_sq, sigma12, scores.avg_ssim);
        edge_diff_map(img1, mu1, img2, mu2, scores.avg_edgediff);
        msssim.scales.push_back(scores);
    }
    return msssim;
}

double score(const Msssim& msssim) {
    constexpr double weight[108] = {0.0,
                                    0.0007376606707406586,
                                    0.0,
                                    0.0,
                                    0.0007793481682867309,
                                    0.0,
                                    0.0,
                                    0.0004371155730107379,
                                    0.0,
                                    1.1041726426657346,
                                    0.00066284834129271,
                                    0.00015231632783718752,
                                    0.0,
                                    0.0016406437456599754,
                                    0.0,
                                    1.8422455520539298,
                                    11.441172603757666,
                                    0.0,
                                    0.0007989109436015163,
                                    0.000176816438078653,
                                    0.0,
                                    1.8787594979546387,
                                    10.94906990605142,
                                    0.0,
                                    0.0007289346991508072,
                                    0.9677937080626833,
                                    0.0,
                                    0.00014003424285435884,
                                    0.9981766977854967,
                                    0.00031949755934435053,
                                    0.0004550992113792063,
                                    0.0,
                                    0.0,
                                    0.0013648766163243398,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    7.466890328078848,
                                    0.0,
                                    17.445833984131262,
                                    0.0006235601634041466,
                                    0.0,
                                    0.0,
                                    6.683678146179332,
                                    0.00037724407979611296,
                                    1.027889937768264,
                                    225.20515300849274,
                                    0.0,
                                    0.0,
                                    19.213238186143016,
                                    0.0011401524586618361,
                                    0.001237755635509985,
                                    176.39317598450694,
                                    0.0,
                                    0.0,
                                    24.43300999870476,
                                    0.28520802612117757,
                                    0.0004485436923833408,
                                    0.0,
                                    0.0,
                                    0.0,
                                    34.77906344483772,
                                    44.835625328877896,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0008680556573291698,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0005313191874358747,
                                    0.0,
                                    0.00016533814161379112,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0004179171803251336,
                                    0.0017290828234722833,
                                    0.0,
                                    0.0020827005846636437,
                                    0.0,
                                    0.0,
                                    8.826982764996862,
                                    23.19243343998926,
                                    0.0,
                                    95.1080498811086,
                                    0.9863978034400682,
                                    0.9834382792465353,
                                    0.0012286405048278493,
                                    171.2667255897307,
                                    0.9807858872435379,
                                    0.0,
                                    0.0,
                                    0.0,
                                    0.0005130064588990679,
                                    0.0,
                                    0.00010854057858411537};

    double ssim = 0.0;
    size_t i = 0;
    for (int c = 0; c < 3; ++c) {
        for (size_t scale = 0; scale < msssim.scales.size(); ++scale) {
            for (int n = 0; n < 2; ++n) {
                ssim += weight[i++] * std::abs(msssim.scales[scale].avg_ssim[c * 2 + n]);
                ssim += weight[i++] * std::abs(msssim.scales[scale].avg_edgediff[c * 4 + n]);
                ssim += weight[i++] * std::abs(msssim.scales[scale].avg_edgediff[c * 4 + n + 2]);
            }
        }
        i += static_cast<size_t>(kNumScales - msssim.scales.size()) * 6;
    }

    ssim *= 0.9562382616834844;
    ssim = 2.326765642916932 * ssim - 0.020884521182843837 * ssim * ssim + 6.248496625763138e-05 * ssim * ssim * ssim;
    if (ssim <= 0.0)
        return 100.0;
    return 100.0 - 10.0 * std::pow(ssim, 0.6276336467831387);
}

} // namespace

std::optional<std::vector<Score>> Ssimulacra2::measure(const Image& ref, const Image& dist) noexcept {
    try {
        if (ref.width < 8 || ref.height < 8)
            return std::nullopt;
        auto ref_rgb = load_rgb24(ref);
        auto dist_rgb = load_rgb24(dist);
        if (!ref_rgb || !dist_rgb)
            return std::nullopt;
        if (ref_rgb->width != dist_rgb->width || ref_rgb->height != dist_rgb->height)
            return std::nullopt;

        const Msssim msssim = compute_ssimulacra2(*ref_rgb, *dist_rgb);
        return std::vector<Score>{{"SSIMULACRA2", static_cast<float>(score(msssim))}};
    } catch (...) {
        return std::nullopt;
    }
}
