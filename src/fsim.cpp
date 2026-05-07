// FSIM / FSIMc: Feature Similarity Index, Zhang, Zhang, Mou, Zhang 2011.
// "FSIM: A Feature Similarity Index for Image Quality Assessment",
// IEEE Transactions on Image Processing, vol. 20, no. 8, pp. 2378-2386.
//
// Phase congruency follows Kovesi's phasecong2 (log-Gabor filters via 2D FFT).
// Higher score = better quality; identical images yield 1.

#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include <fftw3.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace {

constexpr float PI = 3.14159265358979323846f;

struct Rgb24 {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

std::optional<Rgb24> load_rgb24(const std::string& path) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
        return std::nullopt;
    struct FmtGuard {
        ~FmtGuard() { avformat_close_input(&fmt_); }
        AVFormatContext* fmt_;
    } fmt_guard{fmt};

    if (avformat_find_stream_info(fmt, nullptr) < 0)
        return std::nullopt;

    const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vi < 0)
        return std::nullopt;

    const AVCodec* codec = avcodec_find_decoder(fmt->streams[vi]->codecpar->codec_id);
    if (!codec)
        return std::nullopt;

    AVCodecContext* cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, fmt->streams[vi]->codecpar);
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        return std::nullopt;
    }
    struct CcGuard {
        ~CcGuard() { avcodec_free_context(&cc_); }
        AVCodecContext* cc_;
    } cc_guard{cc};

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    struct FrameGuard {
        ~FrameGuard() {
            av_frame_free(&frame_);
            av_packet_free(&pkt_);
        }
        AVFrame* frame_;
        AVPacket* pkt_;
    } frame_guard{frame, pkt};

    bool got_frame = false;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vi)
            if (avcodec_send_packet(cc, pkt) == 0 && avcodec_receive_frame(cc, frame) == 0)
                got_frame = true;
        av_packet_unref(pkt);
        if (got_frame)
            break;
    }
    if (!got_frame)
        return std::nullopt;

    SwsContext* sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, AV_PIX_FMT_RGB24,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws)
        return std::nullopt;

    Rgb24 out;
    out.width = frame->width;
    out.height = frame->height;
    out.pixels.resize(static_cast<size_t>(frame->width) * frame->height * 3);

    uint8_t* dst[4] = {out.pixels.data(), nullptr, nullptr, nullptr};
    int dst_stride[4] = {frame->width * 3, 0, 0, 0};
    sws_scale(sws, (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height, dst, dst_stride);
    sws_freeContext(sws);

    return out;
}

struct Yiq {
    int w, h;
    std::vector<float> y, i, q;
};

// RGB (uint8 0..255) to YIQ with FSIM's adaptive downsample:
//   F = max(1, round(min(W, H) / 256)); FxF average filter, take every F-th pixel.
Yiq rgb_to_yiq_downsampled(const Rgb24& img) {
    const int F = std::max(1, int(std::round(std::min(img.width, img.height) / 256.0)));
    const int dw = img.width / F;
    const int dh = img.height / F;
    Yiq out;
    out.w = dw;
    out.h = dh;
    const size_t n = static_cast<size_t>(dw) * dh;
    out.y.resize(n);
    out.i.resize(n);
    out.q.resize(n);

    const int sw = img.width;
    const float inv_f2 = 1.0f / float(F * F);
    for (int yy = 0; yy < dh; ++yy) {
        for (int xx = 0; xx < dw; ++xx) {
            const int sx = F * xx;
            const int sy = F * yy;
            float r_sum = 0, g_sum = 0, b_sum = 0;
            for (int ky = 0; ky < F; ++ky) {
                for (int kx = 0; kx < F; ++kx) {
                    const int p = ((sy + ky) * sw + (sx + kx)) * 3;
                    r_sum += img.pixels[p + 0];
                    g_sum += img.pixels[p + 1];
                    b_sum += img.pixels[p + 2];
                }
            }
            const float r = r_sum * inv_f2;
            const float g = g_sum * inv_f2;
            const float b = b_sum * inv_f2;
            const size_t k = static_cast<size_t>(yy) * dw + xx;
            out.y[k] = 0.299f * r + 0.587f * g + 0.114f * b;
            out.i[k] = 0.596f * r - 0.274f * g - 0.322f * b;
            out.q[k] = 0.211f * r - 0.523f * g + 0.312f * b;
        }
    }
    return out;
}

// Scharr 3x3 gradient magnitude with replicate (symmetric) padding.
std::vector<float> scharr_magnitude(const std::vector<float>& src, int w, int h) {
    std::vector<float> out(static_cast<size_t>(w) * h);
    auto at = [&](int x, int y) {
        if (x < 0)
            x = 0;
        else if (x >= w)
            x = w - 1;
        if (y < 0)
            y = 0;
        else if (y >= h)
            y = h - 1;
        return src[y * w + x];
    };
    constexpr float kInv16 = 1.0f / 16.0f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float gx = (3.0f * at(x + 1, y - 1) + 10.0f * at(x + 1, y) + 3.0f * at(x + 1, y + 1) - 3.0f * at(x - 1, y - 1) - 10.0f * at(x - 1, y) -
                              3.0f * at(x - 1, y + 1)) *
                             kInv16;
            const float gy = (3.0f * at(x - 1, y + 1) + 10.0f * at(x, y + 1) + 3.0f * at(x + 1, y + 1) - 3.0f * at(x - 1, y - 1) - 10.0f * at(x, y - 1) -
                              3.0f * at(x + 1, y - 1)) *
                             kInv16;
            out[y * w + x] = std::sqrt(gx * gx + gy * gy);
        }
    }
    return out;
}

struct PcWorkspace {
    int w = 0, h = 0;
    fftwf_complex* in = nullptr;
    fftwf_complex* out = nullptr;
    fftwf_complex* img_fft = nullptr;
    fftwf_plan fwd = nullptr;
    fftwf_plan inv = nullptr;
    std::vector<float> radius;
    std::vector<float> sintheta;
    std::vector<float> costheta;
    std::vector<float> log_gabor[4];

    bool init(int W, int H) {
        w = W;
        h = H;
        const int N = W * H;
        in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
        out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
        img_fft = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
        if (!in || !out || !img_fft)
            return false;
        fwd = fftwf_plan_dft_2d(H, W, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
        inv = fftwf_plan_dft_2d(H, W, in, out, FFTW_BACKWARD, FFTW_ESTIMATE);
        if (!fwd || !inv)
            return false;

        radius.resize(N);
        sintheta.resize(N);
        costheta.resize(N);
        for (int s = 0; s < 4; ++s)
            log_gabor[s].resize(N);

        // Frequency grid in FFTW order (DC at index 0, no fftshift).
        std::vector<float> lp(N);
        for (int y = 0; y < H; ++y) {
            const float v = (y < H / 2 ? float(y) : float(y - H)) / float(H);
            for (int x = 0; x < W; ++x) {
                const float u = (x < W / 2 ? float(x) : float(x - W)) / float(W);
                const int idx = y * W + x;
                const float r = std::sqrt(u * u + v * v);
                radius[idx] = r;
                const float theta = std::atan2(-v, u);
                sintheta[idx] = std::sin(theta);
                costheta[idx] = std::cos(theta);
                // Butterworth lowpass cutoff=0.45, n=15.
                const float rn = std::min(r / 0.45f, 100.0f);
                lp[idx] = 1.0f / (1.0f + std::pow(rn, 30.0f));
            }
        }
        // log(0) guard; DC bin is zeroed below regardless.
        radius[0] = 1.0f;

        constexpr float MIN_WAVELENGTH = 6.0f;
        constexpr float MULT = 2.0f;
        constexpr float SIGMA_ONF = 0.55f;
        const float log_sigma_onf_sq = std::log(SIGMA_ONF) * std::log(SIGMA_ONF);

        for (int s = 0; s < 4; ++s) {
            const float wavelength = MIN_WAVELENGTH * std::pow(MULT, float(s));
            const float fo = 1.0f / wavelength;
            for (int i = 0; i < N; ++i) {
                const float lr = std::log(radius[i] / fo);
                log_gabor[s][i] = std::exp(-(lr * lr) / (2.0f * log_sigma_onf_sq)) * lp[i];
            }
            log_gabor[s][0] = 0.0f;
        }
        return true;
    }

    ~PcWorkspace() {
        if (fwd)
            fftwf_destroy_plan(fwd);
        if (inv)
            fftwf_destroy_plan(inv);
        if (in)
            fftwf_free(in);
        if (out)
            fftwf_free(out);
        if (img_fft)
            fftwf_free(img_fft);
    }
};

// Phase congruency a la Kovesi's phasecong2. Output is the per-pixel sum across orientations.
void phase_congruency(PcWorkspace& ws, const std::vector<float>& img, std::vector<float>& pc) {
    const int W = ws.w;
    const int H = ws.h;
    const int N = W * H;
    constexpr int NSCALE = 4;
    constexpr int NORIENT = 4;
    constexpr float K_NOISE = 2.0f;
    constexpr float CUT_OFF = 0.5f;
    constexpr float G_SHARPNESS = 10.0f;
    constexpr float D_THETA_ON_SIGMA = 1.2f;
    constexpr float EPS = 1e-4f;
    constexpr float MULT = 2.0f;

    const float thetaSigma = PI / (NORIENT * D_THETA_ON_SIGMA);

    pc.assign(N, 0.0f);

    // Forward FFT of image.
    for (int i = 0; i < N; ++i) {
        ws.in[i][0] = img[i];
        ws.in[i][1] = 0.0f;
    }
    fftwf_execute(ws.fwd);
    std::memcpy(ws.img_fft, ws.out, sizeof(fftwf_complex) * N);

    std::vector<float> sumE(N), sumO(N), sumAn(N), maxAn(N);
    std::vector<std::vector<float>> EO_real(NSCALE, std::vector<float>(N));
    std::vector<std::vector<float>> EO_imag(NSCALE, std::vector<float>(N));
    std::vector<float> spread(N);
    std::vector<float> amps_scratch(N);

    const float inv_n = 1.0f / float(N);

    for (int o = 0; o < NORIENT; ++o) {
        const float angle_o = float(o) * PI / float(NORIENT);
        const float cos_a = std::cos(angle_o);
        const float sin_a = std::sin(angle_o);
        for (int i = 0; i < N; ++i) {
            // Wrapped angular distance from theta to angle_o.
            const float ds = ws.sintheta[i] * cos_a - ws.costheta[i] * sin_a;
            const float dc = ws.costheta[i] * cos_a + ws.sintheta[i] * sin_a;
            const float dt = std::abs(std::atan2(ds, dc));
            spread[i] = std::exp(-(dt * dt) / (2.0f * thetaSigma * thetaSigma));
        }

        std::fill(sumE.begin(), sumE.end(), 0.0f);
        std::fill(sumO.begin(), sumO.end(), 0.0f);
        std::fill(sumAn.begin(), sumAn.end(), 0.0f);
        std::fill(maxAn.begin(), maxAn.end(), 0.0f);

        float tau = 0.0f;

        for (int s = 0; s < NSCALE; ++s) {
            for (int i = 0; i < N; ++i) {
                const float f = ws.log_gabor[s][i] * spread[i];
                ws.in[i][0] = ws.img_fft[i][0] * f;
                ws.in[i][1] = ws.img_fft[i][1] * f;
            }
            fftwf_execute(ws.inv);
            for (int i = 0; i < N; ++i) {
                const float er = ws.out[i][0] * inv_n;
                const float oi = ws.out[i][1] * inv_n;
                EO_real[s][i] = er;
                EO_imag[s][i] = oi;
                const float amp = std::sqrt(er * er + oi * oi);
                sumE[i] += er;
                sumO[i] += oi;
                sumAn[i] += amp;
                if (s == 0) {
                    maxAn[i] = amp;
                    amps_scratch[i] = amp;
                } else if (amp > maxAn[i]) {
                    maxAn[i] = amp;
                }
            }
        }

        // Noise estimate at smallest scale (s=0).
        std::nth_element(amps_scratch.begin(), amps_scratch.begin() + N / 2, amps_scratch.end());
        const float median = amps_scratch[N / 2];
        tau = median / std::sqrt(std::log(4.0f));

        // Total tau across the geometric sum of scales.
        const float total_tau = tau * (1.0f - std::pow(1.0f / MULT, float(NSCALE))) / (1.0f - 1.0f / MULT);
        const float noise_mean = total_tau * std::sqrt(PI / 2.0f);
        const float noise_sigma = total_tau * std::sqrt((4.0f - PI) / 2.0f);
        float T_noise = noise_mean + K_NOISE * noise_sigma;
        if (T_noise < EPS)
            T_noise = EPS;

        for (int i = 0; i < N; ++i) {
            const float xenergy = std::sqrt(sumE[i] * sumE[i] + sumO[i] * sumO[i]) + EPS;
            const float meanE = sumE[i] / xenergy;
            const float meanO = sumO[i] / xenergy;

            float energy = 0.0f;
            for (int s = 0; s < NSCALE; ++s) {
                const float E = EO_real[s][i];
                const float O = EO_imag[s][i];
                energy += E * meanE + O * meanO - std::abs(E * meanO - O * meanE);
            }
            energy -= T_noise;
            if (energy < 0.0f)
                energy = 0.0f;

            const float width = (sumAn[i] / (maxAn[i] + EPS) - 1.0f) / float(NSCALE - 1);
            const float weight = 1.0f / (1.0f + std::exp(G_SHARPNESS * (CUT_OFF - width)));

            pc[i] += weight * energy / (sumAn[i] + EPS);
        }
    }
}

float compute_fsim(const Rgb24& a, const Rgb24& b, bool with_color) {
    const Yiq ya = rgb_to_yiq_downsampled(a);
    const Yiq yb = rgb_to_yiq_downsampled(b);
    const int W = ya.w;
    const int H = ya.h;
    if (W < 8 || H < 8)
        return 0.0f;

    PcWorkspace ws;
    if (!ws.init(W, H))
        return 0.0f;

    std::vector<float> pc1, pc2;
    phase_congruency(ws, ya.y, pc1);
    phase_congruency(ws, yb.y, pc2);

    const auto g1 = scharr_magnitude(ya.y, W, H);
    const auto g2 = scharr_magnitude(yb.y, W, H);

    constexpr float T1 = 0.85f;
    constexpr float T2 = 160.0f;
    constexpr float T3 = 200.0f;
    constexpr float T4 = 200.0f;
    constexpr float LAMBDA = 0.03f;

    const size_t N = static_cast<size_t>(W) * H;
    double num = 0.0, den = 0.0;
    for (size_t k = 0; k < N; ++k) {
        const float p1 = pc1[k], p2 = pc2[k];
        const float gA = g1[k], gB = g2[k];
        const float s_pc = (2.0f * p1 * p2 + T1) / (p1 * p1 + p2 * p2 + T1);
        const float s_g = (2.0f * gA * gB + T2) / (gA * gA + gB * gB + T2);
        float s_l = s_pc * s_g;
        if (with_color) {
            const float i1 = ya.i[k], i2 = yb.i[k];
            const float q1 = ya.q[k], q2 = yb.q[k];
            const float s_i = (2.0f * i1 * i2 + T3) / (i1 * i1 + i2 * i2 + T3);
            const float s_q = (2.0f * q1 * q2 + T4) / (q1 * q1 + q2 * q2 + T4);
            const float s_c = s_i * s_q;
            // Negative s_c is rare (high color desaturation differences) — clamp before fractional power.
            const float s_c_pos = s_c > 0.0f ? s_c : 0.0f;
            s_l *= std::pow(s_c_pos, LAMBDA);
        }
        const float pcm = std::max(p1, p2);
        num += double(s_l) * double(pcm);
        den += double(pcm);
    }
    if (den <= 0.0)
        return 0.0f;
    return float(num / den);
}

float run(const Image& ref, const Image& dist, bool with_color) {
    if (ref.path.empty() || dist.path.empty())
        return -1.0f;
    auto a = load_rgb24(ref.path);
    auto b = load_rgb24(dist.path);
    if (!a || !b)
        return -1.0f;
    if (a->width != b->width || a->height != b->height)
        return -1.0f;
    return compute_fsim(*a, *b, with_color);
}

} // namespace

std::optional<std::vector<Score>> Fsim::measure(const Image& ref, const Image& dist) noexcept {
    const float s = run(ref, dist, false);
    if (s < 0.0f)
        return std::nullopt;
    return std::vector<Score>{{"FSIM", s}};
}

std::optional<std::vector<Score>> FsimC::measure(const Image& ref, const Image& dist) noexcept {
    const float s = run(ref, dist, true);
    if (s < 0.0f)
        return std::nullopt;
    return std::vector<Score>{{"FSIMc", s}};
}
