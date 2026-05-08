#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include "image.h"
#include "metrics.h"
#include "rgb24.h"

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
    const AVPixelFormat src_fmt = static_cast<AVPixelFormat>(frame->format);

    // Always go through sws with explicit BT.709 + full-range output, regardless of
    // source format. Mixed sources (e.g., PNG-as-RGB vs JPEG-as-YUVJ) otherwise pick
    // up different default matrices/ranges and bias the comparison.
    SwsContext* sws = sws_getContext(frame->width, frame->height, src_fmt, frame->width, frame->height, target_fmt, SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
    if (!sws) {
        std::cerr << "sws_getContext failed: " << path << '\n';
        return std::nullopt;
    }

    const AVPixFmtDescriptor* src_desc = av_pix_fmt_desc_get(src_fmt);
    const bool src_is_rgb = src_desc && (src_desc->flags & AV_PIX_FMT_FLAG_RGB);
    const bool src_is_jpeg_yuv = src_fmt == AV_PIX_FMT_YUVJ420P || src_fmt == AV_PIX_FMT_YUVJ422P || src_fmt == AV_PIX_FMT_YUVJ444P ||
                                 src_fmt == AV_PIX_FMT_YUVJ411P || src_fmt == AV_PIX_FMT_YUVJ440P;
    const bool src_full_range = src_is_rgb || src_is_jpeg_yuv || frame->color_range == AVCOL_RANGE_JPEG;
    const int src_matrix = (frame->colorspace == AVCOL_SPC_BT709) ? SWS_CS_ITU709 : SWS_CS_ITU601;

    sws_setColorspaceDetails(sws, sws_getCoefficients(src_matrix), src_full_range ? 1 : 0, sws_getCoefficients(SWS_CS_ITU709), 1 /* full range */, 0, 1 << 16,
                             1 << 16);

    AVFrame* converted = av_frame_alloc();
    converted->width = frame->width;
    converted->height = frame->height;
    converted->format = target_fmt;
    av_frame_get_buffer(converted, 32);
    sws_scale(sws, (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height, converted->data, converted->linesize);
    sws_freeContext(sws);

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

// Raw planar I420 8-bit: w*h Y, then (w/2)*(h/2) U, then (w/2)*(h/2) V.
// We treat the bytes as already in our normalized space (BT.709 full-range);
// no metadata is available to do otherwise.
static std::optional<Image> load_raw_yuv(const char* path, int w, int h) {
    if (w <= 0 || h <= 0) {
        std::cerr << "raw YUV needs --width and --height (or a peer image with known dimensions): " << path << '\n';
        return std::nullopt;
    }
    if ((w & 1) || (h & 1)) {
        std::cerr << "raw YUV 4:2:0 requires even width and height: " << path << '\n';
        return std::nullopt;
    }

    const size_t y_size = static_cast<size_t>(w) * h;
    const size_t c_size = static_cast<size_t>(w / 2) * (h / 2);
    const size_t total = y_size + 2 * c_size;

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "Cannot open: " << path << '\n';
        return std::nullopt;
    }

    Image img;
    img.width = w;
    img.height = h;
    img.colorspace = ColorSpace::I420;
    img.path = path;
    img.data.resize(total);
    f.read(reinterpret_cast<char*>(img.data.data()), static_cast<std::streamsize>(total));
    if (static_cast<size_t>(f.gcount()) != total) {
        std::cerr << "Short read for raw YUV (expected " << total << " bytes, got " << f.gcount() << "): " << path << '\n';
        return std::nullopt;
    }
    return img;
}

struct Options {
    std::vector<std::string_view> metrics;
    std::string_view ref_path;
    std::string_view dist_path;
    int width = 0;
    int height = 0;
};

static constexpr std::string_view kAllMetrics[] = {"psnr",  "psnr-y", "ssim",  "ms-ssim",  "psnr-hvs", "xpsnr",  "xpsnr-y",
                                                   "fsim",  "fsimc",  "mdsi",  "vmaf",     "vmaf-neg", "ssim2",  "dssim"};

static void add_metric(Options& opts, std::string_view metric) {
    if (std::find(opts.metrics.begin(), opts.metrics.end(), metric) == opts.metrics.end())
        opts.metrics.push_back(metric);
}

static void print_help(std::ostream& os) {
    os << "Usage: eyeq [options] <reference> <distorted>\n"
          "\n"
          "Options:\n"
          "  -h, --help     Show this message and exit\n"
          "  --all          Enable every metric (default: --psnr)\n"
          "\n"
          "Metrics (range; direction):\n"
          "  --psnr         PSNR, full frame (YUV 4:2:0 weighted 4:1:1)         [dB; higher = better, capped at 60 when identical]\n"
          "  --psnr-y       PSNR, Y plane only                                  [dB; higher = better]\n"
          "  --ssim         Structural Similarity Index (Y)                     [0..1; higher = better, 1 = identical]\n"
          "  --ms-ssim      Multi-Scale SSIM (Y)                                [0..1; higher = better, 1 = identical]\n"
          "  --psnr-hvs     PSNR with Human Visual System weighting             [dB; higher = better]\n"
          "  --xpsnr        Extended Perceptually Weighted PSNR (HHI)           [dB; higher = better]\n"
          "  --xpsnr-y      XPSNR, Y plane only                                 [dB; higher = better]\n"
          "  --fsim         Feature Similarity Index, luminance only            [0..1; higher = better, 1 = identical]\n"
          "  --fsimc        FSIM with chromatic component (YIQ)                 [0..1; higher = better, 1 = identical]\n"
          "  --mdsi         Mean Deviation Similarity Index                     [~0..0.5; LOWER = better, 0 = identical]\n"
          "  --vmaf         VMAF (model vmaf_v0.6.1)                            [~0..100; higher = better]\n"
          "  --vmaf-neg     VMAF-NEG, less gameable by enhancement (vmaf_v0.6.1neg) [~0..100; higher = better]\n"
          "  --ssim2        SSIMULACRA 2.1 (alias --ssimulacra2)                [~-inf..100; higher = better, >=90 visually identical]\n"
          "  --dssim        Multi-scale L*a*b* structural dissimilarity         [>=0; LOWER = better, 0 = identical]\n"
          "\n"
          "Raw YUV inputs (.yuv, planar I420 8-bit):\n"
          "  --width N      Width in pixels  (omit when paired with an image of known dimensions)\n"
          "  --height N     Height in pixels (omit when paired with an image of known dimensions)\n"
          "\n"
          "No flags defaults to --psnr only. Inputs are converted to YUV 4:2:0, BT.709, full range.\n"
          "Raw YUV is assumed already in that space (no metadata to do otherwise).\n";
}

static bool parse_args(Options& opts, int argc, char* argv[]) {
    if (argc <= 1) {
        print_help(std::cout);
        std::exit(0);
    }

    std::vector<std::string_view> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help(std::cout);
            std::exit(0);
        } else if (arg == "--psnr")
            add_metric(opts, "psnr");
        else if (arg == "--psnr-y")
            add_metric(opts, "psnr-y");
        else if (arg == "--ssim")
            add_metric(opts, "ssim");
        else if (arg == "--ms-ssim")
            add_metric(opts, "ms-ssim");
        else if (arg == "--psnr-hvs")
            add_metric(opts, "psnr-hvs");
        else if (arg == "--xpsnr")
            add_metric(opts, "xpsnr");
        else if (arg == "--xpsnr-y")
            add_metric(opts, "xpsnr-y");
        else if (arg == "--mdsi")
            add_metric(opts, "mdsi");
        else if (arg == "--fsim")
            add_metric(opts, "fsim");
        else if (arg == "--fsimc")
            add_metric(opts, "fsimc");
        else if (arg == "--vmaf")
            add_metric(opts, "vmaf");
        else if (arg == "--vmaf-neg")
            add_metric(opts, "vmaf-neg");
        else if (arg == "--ssim2" || arg == "--ssimulacra2")
            add_metric(opts, "ssim2");
        else if (arg == "--dssim")
            add_metric(opts, "dssim");
        else if (arg == "--width") {
            if (i + 1 >= argc) {
                std::cerr << "--width requires a value\n";
                return false;
            }
            opts.width = std::atoi(argv[++i]);
        } else if (arg == "--height") {
            if (i + 1 >= argc) {
                std::cerr << "--height requires a value\n";
                return false;
            }
            opts.height = std::atoi(argv[++i]);
        } else if (arg == "--all") {
            for (const auto metric: kAllMetrics)
                add_metric(opts, metric);
        } else if (arg.starts_with("--")) {
            std::cerr << "Unknown option: " << arg << '\n';
            return false;
        } else
            positional.push_back(arg);
    }

    if (positional.size() < 2) {
        print_help(std::cerr);
        return false;
    }

    if (opts.metrics.empty())
        add_metric(opts, "psnr");

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

    const std::string ref_path(opts.ref_path);
    const std::string dist_path(opts.dist_path);
    const bool ref_yuv = is_raw_yuv_path(opts.ref_path);
    const bool dist_yuv = is_raw_yuv_path(opts.dist_path);

    std::optional<Image> ref, dist;
    if (ref_yuv && dist_yuv) {
        if (opts.width <= 0 || opts.height <= 0) {
            std::cerr << "Both inputs are raw YUV; --width and --height are required\n";
            return 1;
        }
        ref = load_raw_yuv(ref_path.c_str(), opts.width, opts.height);
        dist = load_raw_yuv(dist_path.c_str(), opts.width, opts.height);
    } else if (ref_yuv) {
        dist = load_image(dist_path.c_str(), cs);
        if (!dist)
            return 1;
        const int w = opts.width > 0 ? opts.width : dist->width;
        const int h = opts.height > 0 ? opts.height : dist->height;
        ref = load_raw_yuv(ref_path.c_str(), w, h);
    } else if (dist_yuv) {
        ref = load_image(ref_path.c_str(), cs);
        if (!ref)
            return 1;
        const int w = opts.width > 0 ? opts.width : ref->width;
        const int h = opts.height > 0 ? opts.height : ref->height;
        dist = load_raw_yuv(dist_path.c_str(), w, h);
    } else {
        ref = load_image(ref_path.c_str(), cs);
        dist = load_image(dist_path.c_str(), cs);
    }

    if (!ref || !dist)
        return 1;

    if (ref->width != dist->width || ref->height != dist->height) {
        std::cerr << "Dimension mismatch: " << ref->width << "x" << ref->height << " vs " << dist->width << "x" << dist->height << '\n';
        return 1;
    }

    int failed = 0;
    for (const auto& metric_name: opts.metrics) {
        auto metric = MetricsFactory::create(metric_name, cs, ref->width, ref->height);
        if (!metric) {
            std::cerr << "Unknown metric: " << metric_name << '\n';
            ++failed;
            continue;
        }

        auto scores = metric->measure(*ref, *dist);
        if (!scores) {
            std::cerr << metric_name << ": computation failed\n";
            ++failed;
            continue;
        }

        for (const auto& s: *scores) {
            if (std::isinf(s.value))
                std::cout << s.label << ": inf (identical)\n";
            else
                std::cout << s.label << ": " << s.value << '\n';
        }
    }

    return failed > 0 ? 1 : 0;
}
