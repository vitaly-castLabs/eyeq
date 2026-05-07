// MDSI: Mean Deviation Similarity Index, Nafchi et al. 2016.
// "Mean Deviation Similarity Index: Efficient and Reliable Full-Reference
//  Image Quality Evaluator", IEEE Access, vol. 4, pp. 5579-5590.
// Lower is better; identical images yield 0.

#include "metrics.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace {

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

// Convert RGB to MDSI's opponent color planes L, M, N.
struct Lmn {
    int w, h;
    std::vector<float> l, m, n;
};

Lmn rgb_to_lmn(const Rgb24& img) {
    Lmn out;
    out.w = img.width;
    out.h = img.height;
    const size_t n_px = static_cast<size_t>(img.width) * img.height;
    out.l.resize(n_px);
    out.m.resize(n_px);
    out.n.resize(n_px);
    for (size_t i = 0; i < n_px; ++i) {
        const float r = img.pixels[3 * i + 0];
        const float g = img.pixels[3 * i + 1];
        const float b = img.pixels[3 * i + 2];
        out.l[i] = 0.2989f * r + 0.5870f * g + 0.1140f * b;
        out.m[i] = 0.30f * r + 0.04f * g - 0.35f * b;
        out.n[i] = 0.34f * r - 0.60f * g + 0.17f * b;
    }
    return out;
}

// 2x2 average-pool downsample (matches the MATLAB reference's blockproc/imresize).
std::vector<float> downsample_2x2(const std::vector<float>& src, int w, int h, int& dw, int& dh) {
    dw = w / 2;
    dh = h / 2;
    std::vector<float> out(static_cast<size_t>(dw) * dh);
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const int sx = 2 * x;
            const int sy = 2 * y;
            const float s = src[sy * w + sx] + src[sy * w + sx + 1] + (sy + 1 < h ? src[(sy + 1) * w + sx] + src[(sy + 1) * w + sx + 1] : 0.0f);
            out[y * dw + x] = s * 0.25f;
        }
    }
    return out;
}

// 3x3 Prewitt gradient magnitude with replicate padding.
std::vector<float> prewitt_magnitude(const std::vector<float>& src, int w, int h) {
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
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float gx = (at(x + 1, y - 1) + at(x + 1, y) + at(x + 1, y + 1)) - (at(x - 1, y - 1) + at(x - 1, y) + at(x - 1, y + 1));
            const float gy = (at(x - 1, y + 1) + at(x, y + 1) + at(x + 1, y + 1)) - (at(x - 1, y - 1) + at(x, y - 1) + at(x + 1, y - 1));
            out[y * w + x] = std::sqrt(gx * gx + gy * gy) / 3.0f;
        }
    }
    return out;
}

} // namespace

std::optional<std::vector<Score>> Mdsi::measure(const Image& ref, const Image& dist) noexcept {
    if (ref.path.empty() || dist.path.empty())
        return std::nullopt;

    auto ref_rgb = load_rgb24(ref.path);
    auto dist_rgb = load_rgb24(dist.path);
    if (!ref_rgb || !dist_rgb)
        return std::nullopt;
    if (ref_rgb->width != dist_rgb->width || ref_rgb->height != dist_rgb->height)
        return std::nullopt;

    const auto lmn_r = rgb_to_lmn(*ref_rgb);
    const auto lmn_d = rgb_to_lmn(*dist_rgb);

    int dw = 0, dh = 0;
    auto l_r = downsample_2x2(lmn_r.l, lmn_r.w, lmn_r.h, dw, dh);
    auto l_d = downsample_2x2(lmn_d.l, lmn_d.w, lmn_d.h, dw, dh);
    auto m_r = downsample_2x2(lmn_r.m, lmn_r.w, lmn_r.h, dw, dh);
    auto m_d = downsample_2x2(lmn_d.m, lmn_d.w, lmn_d.h, dw, dh);
    auto n_r = downsample_2x2(lmn_r.n, lmn_r.w, lmn_r.h, dw, dh);
    auto n_d = downsample_2x2(lmn_d.n, lmn_d.w, lmn_d.h, dw, dh);
    if (dw < 3 || dh < 3)
        return std::nullopt;

    const size_t n_px = static_cast<size_t>(dw) * dh;
    std::vector<float> l_f(n_px);
    for (size_t i = 0; i < n_px; ++i)
        l_f[i] = 0.5f * (l_r[i] + l_d[i]);

    const auto g_r = prewitt_magnitude(l_r, dw, dh);
    const auto g_d = prewitt_magnitude(l_d, dw, dh);
    const auto g_f = prewitt_magnitude(l_f, dw, dh);

    constexpr float C1 = 140.0f;
    constexpr float C2 = 55.0f;
    constexpr float C3 = 550.0f;
    constexpr float alpha = 0.6f;

    std::vector<float> gcs(n_px);
    for (size_t i = 0; i < n_px; ++i) {
        const float gr = g_r[i];
        const float gd = g_d[i];
        const float gf = g_f[i];
        const float gs_rd = (2.0f * gr * gd + C1) / (gr * gr + gd * gd + C1);
        const float gs_rf = (2.0f * gr * gf + C2) / (gr * gr + gf * gf + C2);
        const float gs_df = (2.0f * gd * gf + C2) / (gd * gd + gf * gf + C2);
        const float gs = gs_rd + gs_rf - gs_df;

        const float mr = m_r[i], md = m_d[i], nr = n_r[i], nd = n_d[i];
        const float cs = (2.0f * (mr * md + nr * nd) + C3) / (mr * mr + md * md + nr * nr + nd * nd + C3);

        gcs[i] = alpha * gs + (1.0f - alpha) * cs;
    }

    // Deviation pooling: q = 1/4. MDSI = ( mean( |GCS^(1/4) - mean(GCS^(1/4))|^4 ) )^(1/4)
    double sum = 0.0;
    for (size_t i = 0; i < n_px; ++i) {
        // GCS can be slightly negative for highly distorted regions; clamp to 0 before fractional power.
        const float v = gcs[i] > 0.0f ? gcs[i] : 0.0f;
        sum += std::pow((double)v, 0.25);
    }
    const double mu = sum / (double)n_px;

    double dev_sum = 0.0;
    for (size_t i = 0; i < n_px; ++i) {
        const float v = gcs[i] > 0.0f ? gcs[i] : 0.0f;
        const double d = std::pow((double)v, 0.25) - mu;
        const double d2 = d * d;
        dev_sum += d2 * d2;
    }
    const double mdsi = std::pow(dev_sum / (double)n_px, 0.25);

    return std::vector<Score>{{"MDSI", static_cast<float>(mdsi)}};
}
