#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "image.h"
#include "metrics.h"

// image loading via ffmpeg (ONLY for loading + colorspace conversion)
static std::optional<Image> load_image(const char* path, ColorSpace cs) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) {
        std::cerr << "Cannot open: " << path << '\n';
        return std::nullopt;
    }

    struct FmtGuard {
        ~FmtGuard() { avformat_close_input(&fmt_); }
        AVFormatContext* fmt_;
    } fmt_guard{fmt};

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        std::cerr << "No stream info: " << path << '\n';
        return std::nullopt;
    }

    const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vi < 0) {
        std::cerr << "No video stream: " << path << '\n';
        return std::nullopt;
    }

    const AVCodec* codec = avcodec_find_decoder(fmt->streams[vi]->codecpar->codec_id);
    if (!codec) {
        std::cerr << "No decoder: " << path << '\n';
        return std::nullopt;
    }

    AVCodecContext* cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, fmt->streams[vi]->codecpar);
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        std::cerr << "Cannot open codec: " << path << '\n';
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

    if (!got_frame) {
        std::cerr << "Cannot decode frame: " << path << '\n';
        return std::nullopt;
    }

    const AVPixelFormat target_fmt = color_space_to_av_pix_fmt(cs);

    AVFrame* converted = nullptr;
    if (frame->format != target_fmt) {
        SwsContext* sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, target_fmt,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) {
            std::cerr << "sws_getContext failed: " << path << '\n';
            return std::nullopt;
        }

        converted = av_frame_alloc();
        converted->width = frame->width;
        converted->height = frame->height;
        converted->format = target_fmt;
        av_frame_get_buffer(converted, 32);

        sws_scale(sws, (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height, converted->data, converted->linesize);
        sws_freeContext(sws);
    } else {
        converted = av_frame_clone(frame);
    }

    struct ConvGuard {
        ~ConvGuard() { av_frame_free(&frame_); }
        AVFrame* frame_;
    } conv_guard{converted};

    Image img;
    img.width = converted->width;
    img.height = converted->height;
    img.colorspace = cs;
    img.path = path;

    int plane_sizes[3];
    for (int i = 0; i < 3; ++i) {
        int w = converted->width;
        int h = converted->height;
        if (cs == ColorSpace::I420 && i >= 1) {
            w = (w + 1) / 2;
            h = (h + 1) / 2;
        }
        plane_sizes[i] = w * h;
    }

    const int total_size = plane_sizes[0] + plane_sizes[1] + plane_sizes[2];
    img.data.resize(static_cast<size_t>(total_size));

    size_t offset = 0;
    for (int i = 0; i < 3; ++i) {
        int w = converted->width;
        int h = converted->height;
        if (cs == ColorSpace::I420 && i >= 1) {
            w = (w + 1) / 2;
            h = (h + 1) / 2;
        }
        const size_t plane_size = static_cast<size_t>(w * h);
        for (int row = 0; row < h; ++row)
            std::memcpy(img.data.data() + offset + row * w, converted->data[i] + row * converted->linesize[i], static_cast<size_t>(w));
        offset += plane_size;
    }

    return img;
}

struct Options {
    std::vector<std::string_view> metrics;
    std::string_view ref_path;
    std::string_view dist_path;
};

static bool parse_args(Options& opts, int argc, char* argv[]) {
    std::vector<std::string_view> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--psnr")
            opts.metrics.push_back("psnr");
        else if (arg == "--psnr-y")
            opts.metrics.push_back("psnr-y");
        else if (arg == "--ssim")
            opts.metrics.push_back("ssim");
        else if (arg == "--psnr-hvs")
            opts.metrics.push_back("psnr-hvs");
        else if (arg == "--vmaf")
            opts.metrics.push_back("vmaf");
        else if (arg == "--ssim2")
            opts.metrics.push_back("ssim2");
        else
            positional.push_back(arg);
    }

    if (opts.metrics.empty())
        opts.metrics.push_back("psnr");

    if (positional.size() < 2) {
        std::cerr << "Usage: eyeq [--psnr] [--psnr-y] [--ssim] [--psnr-hvs] [--vmaf] [--ssim2] <reference> <distorted>\n";
        return false;
    }

    opts.ref_path = positional[0];
    opts.dist_path = positional[1];
    return true;
}

int main(int argc, char* argv[]) {
    av_log_set_level(AV_LOG_ERROR);

    Options opts;
    if (!parse_args(opts, argc, argv))
        return 1;

    const ColorSpace cs = ColorSpace::I420;

    auto ref = load_image(std::string(opts.ref_path).c_str(), cs);
    auto dist = load_image(std::string(opts.dist_path).c_str(), cs);
    if (!ref || !dist)
        return 1;

    if (ref->width != dist->width || ref->height != dist->height) {
        std::cerr << "Dimension mismatch: " << ref->width << "x" << ref->height << " vs " << dist->width << "x" << dist->height << '\n';
        return 1;
    }

    for (const auto& metric_name: opts.metrics) {
        auto metric = MetricsFactory::create(metric_name, cs, ref->width, ref->height);
        if (!metric) {
            std::cerr << "Unknown metric: " << metric_name << '\n';
            return 1;
        }

        auto scores = metric->measure(*ref, *dist);
        if (!scores) {
            std::cerr << metric_name << ": computation failed\n";
            return 1;
        }

        for (const auto& s: *scores) {
            if (std::isinf(s.value))
                std::cout << s.label << ": inf (identical)\n";
            else
                std::cout << s.label << ": " << s.value << '\n';
        }
    }

    return 0;
}
