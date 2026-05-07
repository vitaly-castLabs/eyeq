// XPSNR algorithm ported from FFmpeg's libavfilter/vf_xpsnr.c
//   Copyright (c) 2024 Christian R. Helmrich
//   Copyright (c) 2024 Christian Lehmann
//   Copyright (c) 2024 Christian Stoffers
//   Authors: Helmrich, Lehmann, Stoffers, Fraunhofer HHI, Berlin, Germany
// Original is LGPL 2.1+. This port keeps the same license; the rest of eyeq
// is under its own LICENSE in the repo root.

#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

constexpr int XPSNR_GAMMA = 2;

// Downsampled (2x2) high-pass spatial activity, used when picture > HD.
uint64_t highds(int x_act, int y_act, int w_act, int h_act, const int16_t* o_m0, int o) {
    uint64_t sa_act = 0;
    for (int y = y_act; y < h_act; y += 2) {
        for (int x = x_act; x < w_act; x += 2) {
            const int f = 12 * ((int)o_m0[ y   *o + x  ] + (int)o_m0[ y   *o + x+1] + (int)o_m0[(y+1)*o + x  ] + (int)o_m0[(y+1)*o + x+1])
                         - 3 * ((int)o_m0[(y-1)*o + x  ] + (int)o_m0[(y-1)*o + x+1] + (int)o_m0[(y+2)*o + x  ] + (int)o_m0[(y+2)*o + x+1])
                         - 3 * ((int)o_m0[ y   *o + x-1] + (int)o_m0[ y   *o + x+2] + (int)o_m0[(y+1)*o + x-1] + (int)o_m0[(y+1)*o + x+2])
                         - 2 * ((int)o_m0[(y-1)*o + x-1] + (int)o_m0[(y-1)*o + x+2] + (int)o_m0[(y+2)*o + x-1] + (int)o_m0[(y+2)*o + x+2])
                             - ((int)o_m0[(y-2)*o + x-1] + (int)o_m0[(y-2)*o + x  ] + (int)o_m0[(y-2)*o + x+1] + (int)o_m0[(y-2)*o + x+2]
                              + (int)o_m0[(y+3)*o + x-1] + (int)o_m0[(y+3)*o + x  ] + (int)o_m0[(y+3)*o + x+1] + (int)o_m0[(y+3)*o + x+2]
                              + (int)o_m0[(y-1)*o + x-2] + (int)o_m0[ y   *o + x-2] + (int)o_m0[(y+1)*o + x-2] + (int)o_m0[(y+2)*o + x-2]
                              + (int)o_m0[(y-1)*o + x+3] + (int)o_m0[ y   *o + x+3] + (int)o_m0[(y+1)*o + x+3] + (int)o_m0[(y+2)*o + x+3]);
            sa_act += (uint64_t) std::abs(f);
        }
    }
    return sa_act;
}

uint64_t diff1st(uint32_t w_act, uint32_t h_act, const int16_t* o_m0, int16_t* o_m1, int o) {
    uint64_t ta_act = 0;
    for (uint32_t y = 0; y < h_act; y += 2) {
        for (uint32_t x = 0; x < w_act; x += 2) {
            const int t = (int)o_m0[y*o + x] + (int)o_m0[y*o + x+1] + (int)o_m0[(y+1)*o + x] + (int)o_m0[(y+1)*o + x+1]
                       - ((int)o_m1[y*o + x] + (int)o_m1[y*o + x+1] + (int)o_m1[(y+1)*o + x] + (int)o_m1[(y+1)*o + x+1]);
            ta_act += (uint64_t) std::abs(t);
            o_m1[y*o + x  ] = o_m0[y*o + x  ];  o_m1[(y+1)*o + x  ] = o_m0[(y+1)*o + x  ];
            o_m1[y*o + x+1] = o_m0[y*o + x+1];  o_m1[(y+1)*o + x+1] = o_m0[(y+1)*o + x+1];
        }
    }
    return ta_act * XPSNR_GAMMA;
}

uint64_t sse_line(const int16_t* a, const int16_t* b, int w) {
    uint64_t s = 0;
    for (int i = 0; i < w; ++i) {
        const int d = (int)a[i] - (int)b[i];
        s += (uint64_t) (d * d);
    }
    return s;
}

uint64_t calc_squared_error(const int16_t* org, uint32_t s_org, const int16_t* rec, uint32_t s_rec, uint32_t bw, uint32_t bh) {
    uint64_t sse = 0;
    for (uint32_t y = 0; y < bh; ++y) {
        sse += sse_line(org, rec, (int) bw);
        org += s_org;
        rec += s_rec;
    }
    return sse;
}

double calc_sse_and_weight(const int16_t* pic_org, uint32_t s_org, int16_t* pic_org_m1,
                           const int16_t* pic_rec, uint32_t s_rec,
                           uint32_t off_x, uint32_t off_y, uint32_t bw, uint32_t bh,
                           int plane0_w, int plane0_h, int depth, double* ms_act) {
    const int o = (int) s_org;
    const int r = (int) s_rec;
    const int16_t* o_m0 = pic_org    + off_y * o + off_x;
    int16_t*       o_m1 = pic_org_m1 + off_y * o + off_x;
    const int16_t* r_m0 = pic_rec    + off_y * r + off_x;

    const int b_val = (plane0_w * plane0_h > 2048 * 1152 ? 2 : 1);
    const int x_act = (off_x > 0 ? 0 : b_val);
    const int y_act = (off_y > 0 ? 0 : b_val);
    const int w_act = (off_x + bw < (uint32_t) plane0_w ? (int) bw : (int) bw - b_val);
    const int h_act = (off_y + bh < (uint32_t) plane0_h ? (int) bh : (int) bh - b_val);

    const double sse = (double) calc_squared_error(o_m0, s_org, r_m0, s_rec, bw, bh);

    if (w_act <= x_act || h_act <= y_act)
        return sse;

    uint64_t sa_act = 0;
    if (b_val > 1) {
        if (w_act > 12)
            sa_act = highds(x_act, y_act, w_act, h_act, o_m0, o);
        else
            sa_act = highds(x_act, y_act, w_act, h_act, o_m0, o);
    } else {
        for (int y = y_act; y < h_act; ++y) {
            for (int x = x_act; x < w_act; ++x) {
                const int f = 12 * (int)o_m0[y*o + x] - 2 * ((int)o_m0[y*o + x-1] + (int)o_m0[y*o + x+1] + (int)o_m0[(y-1)*o + x] + (int)o_m0[(y+1)*o + x])
                                 - ((int)o_m0[(y-1)*o + x-1] + (int)o_m0[(y-1)*o + x+1] + (int)o_m0[(y+1)*o + x-1] + (int)o_m0[(y+1)*o + x+1]);
                sa_act += (uint64_t) std::abs(f);
            }
        }
    }

    *ms_act = (double) sa_act / ((double) (w_act - x_act) * (double) (h_act - y_act));

    // Single-frame mode: temporal activity uses 1st-order diff against a zeroed
    // history buffer, matching FFmpeg's xpsnr filter behavior on the first frame.
    uint64_t ta_act = 0;
    if (b_val > 1) {
        ta_act = diff1st(bw, bh, o_m0, o_m1, o);
    } else {
        for (uint32_t y = 0; y < bh; ++y) {
            for (uint32_t x = 0; x < bw; ++x) {
                const int t = (int)o_m0[y * o + x] - (int)o_m1[y * o + x];
                ta_act += XPSNR_GAMMA * (uint64_t) std::abs(t);
                o_m1[y * o + x] = o_m0[y * o + x];
            }
        }
    }

    *ms_act += (double) ta_act / ((double) bw * (double) bh);

    if (*ms_act < (double) (1 << (depth - 6)))
        *ms_act = (double) (1 << (depth - 6));

    *ms_act *= *ms_act;
    return sse;
}

double get_avg_xpsnr(double sqrt_wsse, uint32_t img_w, uint32_t img_h, uint64_t max_error) {
    if (sqrt_wsse <= 0.0)
        return INFINITY;
    const uint64_t num = (uint64_t) img_w * (uint64_t) img_h * max_error;
    return 10.0 * std::log10((double) num / (sqrt_wsse * sqrt_wsse));
}

}

std::optional<XpsnrBase::Planes> XpsnrBase::compute(const Image& ref, const Image& dist) noexcept {
    if (colorspace_ != ColorSpace::I420)
        return std::nullopt;

    constexpr int depth = 8;
    constexpr uint64_t max_error_64 = 255ull * 255ull;

    const int w = ref.plane_width(0);
    const int h = ref.plane_height(0);
    if (w <= 0 || h <= 0)
        return std::nullopt;

    // XPSNR block size: 4 * round(32 * sqrt(area / UHD)) -> integer multiple of 4.
    const double r_pic = (double) (w * h) / (3840.0 * 2160.0);
    const uint32_t b = std::max(0, 4 * (int32_t) (32.0 * std::sqrt(r_pic) + 0.5));
    const uint32_t w_blk = (w + b - 1) / b;
    const uint32_t h_blk = (h + b - 1) / b;
    const double avg_act = std::sqrt(16.0 * (double) (1 << (2 * depth - 9)) / std::sqrt(std::max(0.00001, r_pic)));

    // Convert each plane (8-bit) to int16_t buffers, stride == plane_width.
    std::vector<int16_t> org_buf[3];
    std::vector<int16_t> rec_buf[3];
    for (int c = 0; c < 3; ++c) {
        const int pw = ref.plane_width(c);
        const int ph = ref.plane_height(c);
        org_buf[c].resize(static_cast<size_t>(pw) * ph);
        rec_buf[c].resize(static_cast<size_t>(pw) * ph);
        const uint8_t* src_o = ref.data.data() + ref.plane_offset(c);
        const uint8_t* src_r = dist.data.data() + dist.plane_offset(c);
        for (int y = 0; y < ph; ++y) {
            for (int x = 0; x < pw; ++x) {
                org_buf[c][y * pw + x] = (int16_t) src_o[y * pw + x];
                rec_buf[c][y * pw + x] = (int16_t) src_r[y * pw + x];
            }
        }
    }

    // Zero-initialized 1st-order history buffer for the luma plane (matches FFmpeg
    // single-frame behavior; for video, this would persist across frames).
    std::vector<int16_t> org_m1(static_cast<size_t>(w) * h, 0);

    std::vector<double> sse_luma(w_blk * h_blk, 0.0);
    std::vector<double> weights(w_blk * h_blk, 0.0);

    uint64_t wsse64[3] = {0, 0, 0};

    if (b >= 4) {
        const int16_t* p_org = org_buf[0].data();
        const int16_t* p_rec = rec_buf[0].data();
        const uint32_t s_org = (uint32_t) w;
        const uint32_t s_rec = (uint32_t) w;
        double wsse_luma = 0.0;
        uint32_t idx_blk = 0;

        for (uint32_t y = 0; y < (uint32_t) h; y += b) {
            const uint32_t bh = (y + b > (uint32_t) h ? (uint32_t) h - y : b);
            for (uint32_t x = 0; x < (uint32_t) w; x += b, ++idx_blk) {
                const uint32_t bw = (x + b > (uint32_t) w ? (uint32_t) w - x : b);
                double ms_act = 1.0;

                sse_luma[idx_blk] = calc_sse_and_weight(p_org, s_org, org_m1.data(),
                                                       p_rec, s_rec,
                                                       x, y, bw, bh, w, h, depth, &ms_act);
                weights[idx_blk] = 1.0 / std::sqrt(ms_act);

                // In-line "min-smoothing" for low resolutions, per the paper.
                if ((uint32_t) (w * h) <= 640 * 480) {
                    double ms_act_prev = 0.0;
                    if (x == 0)
                        ms_act_prev = (idx_blk > 1 ? weights[idx_blk - 2] : 0);
                    else
                        ms_act_prev = (x > b ? std::max(weights[idx_blk - 2], weights[idx_blk]) : weights[idx_blk]);

                    if (idx_blk > w_blk)
                        ms_act_prev = std::max(ms_act_prev, weights[idx_blk - 1 - w_blk]);
                    if ((idx_blk > 0) && (weights[idx_blk - 1] > ms_act_prev))
                        weights[idx_blk - 1] = ms_act_prev;

                    if ((x + b >= (uint32_t) w) && (y + b >= (uint32_t) h) && (idx_blk > w_blk)) {
                        ms_act_prev = std::max(weights[idx_blk - 1], weights[idx_blk - w_blk]);
                        if (weights[idx_blk] > ms_act_prev)
                            weights[idx_blk] = ms_act_prev;
                    }
                }
            }
        }

        idx_blk = 0;
        for (uint32_t y = 0; y < (uint32_t) h; y += b) {
            for (uint32_t x = 0; x < (uint32_t) w; x += b, ++idx_blk)
                wsse_luma += sse_luma[idx_blk] * weights[idx_blk];
        }
        wsse64[0] = (wsse_luma <= 0.0 ? 0 : (uint64_t) (wsse_luma * avg_act + 0.5));
    }

    for (int c = 0; c < 3; ++c) {
        const int pw = ref.plane_width(c);
        const int ph = ref.plane_height(c);
        const int16_t* p_org = org_buf[c].data();
        const int16_t* p_rec = rec_buf[c].data();
        const uint32_t s_org = (uint32_t) pw;
        const uint32_t s_rec = (uint32_t) pw;

        if (b < 4) {
            wsse64[c] = calc_squared_error(p_org, s_org, p_rec, s_rec, pw, ph);
        } else if (c > 0) {
            const uint32_t bx = (b * (uint32_t) pw) / (uint32_t) w;
            const uint32_t by = (b * (uint32_t) ph) / (uint32_t) h;
            double wsse_chroma = 0.0;
            uint32_t idx_blk = 0;

            for (uint32_t y = 0; y < (uint32_t) ph; y += by) {
                const uint32_t bh = (y + by > (uint32_t) ph ? (uint32_t) ph - y : by);
                for (uint32_t x = 0; x < (uint32_t) pw; x += bx, ++idx_blk) {
                    const uint32_t bw = (x + bx > (uint32_t) pw ? (uint32_t) pw - x : bx);
                    wsse_chroma += (double) calc_squared_error(p_org + y * s_org + x, s_org,
                                                              p_rec + y * s_rec + x, s_rec,
                                                              bw, bh) * weights[idx_blk];
                }
            }
            wsse64[c] = (wsse_chroma <= 0.0 ? 0 : (uint64_t) (wsse_chroma * avg_act + 0.5));
        }
    }

    double xpsnr_per_comp[3];
    for (int c = 0; c < 3; ++c) {
        const double sqrt_wsse = std::sqrt((double) wsse64[c]);
        xpsnr_per_comp[c] = get_avg_xpsnr(sqrt_wsse, (uint32_t) ref.plane_width(c), (uint32_t) ref.plane_height(c), max_error_64);
    }

    return Planes{
        static_cast<float>(xpsnr_per_comp[0]),
        static_cast<float>(xpsnr_per_comp[1]),
        static_cast<float>(xpsnr_per_comp[2]),
    };
}

std::optional<std::vector<Score>> Xpsnr::measure(const Image& ref, const Image& dist) noexcept {
    auto p = compute(ref, dist);
    if (!p)
        return std::nullopt;

    auto mse_from_psnr = [](double q) { return (255.0 * 255.0) / std::pow(10.0, q / 10.0); };
    const double mse_full = (4.0 * mse_from_psnr(p->y) + mse_from_psnr(p->u) + mse_from_psnr(p->v)) / 6.0;
    const double xpsnr_full = 10.0 * std::log10((255.0 * 255.0) / mse_full);
    return std::vector<Score>{{"XPSNR", static_cast<float>(xpsnr_full)}};
}

std::optional<std::vector<Score>> XpsnrY::measure(const Image& ref, const Image& dist) noexcept {
    auto p = compute(ref, dist);
    if (!p)
        return std::nullopt;
    return std::vector<Score>{{"XPSNR (Y)", p->y}};
}
