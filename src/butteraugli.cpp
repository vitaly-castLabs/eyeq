#include "metrics.h"

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

#include <jxl/butteraugli.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/types.h>

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

}

std::optional<std::vector<Score>> Butteraugli::measure(const Image& ref, const Image& dist) noexcept {
    if (ref.path.empty() || dist.path.empty())
        return std::nullopt;

    auto ref_rgb = load_rgb24(ref.path);
    auto dist_rgb = load_rgb24(dist.path);
    if (!ref_rgb || !dist_rgb)
        return std::nullopt;
    if (ref_rgb->width != dist_rgb->width || ref_rgb->height != dist_rgb->height)
        return std::nullopt;

    void* runner = JxlThreadParallelRunnerCreate(nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());
    JxlButteraugliApi* api = JxlButteraugliApiCreate(nullptr);
    if (!api) {
        if (runner)
            JxlThreadParallelRunnerDestroy(runner);
        return std::nullopt;
    }
    if (runner)
        JxlButteraugliApiSetParallelRunner(api, JxlThreadParallelRunner, runner);
    JxlButteraugliApiSetIntensityTarget(api, 80.0f);
    JxlButteraugliApiSetHFAsymmetry(api, 0.8f);

    JxlPixelFormat pf{3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    JxlButteraugliResult* result = JxlButteraugliCompute(api, static_cast<uint32_t>(ref_rgb->width), static_cast<uint32_t>(ref_rgb->height), &pf,
                                                         ref_rgb->pixels.data(), ref_rgb->pixels.size(), &pf, dist_rgb->pixels.data(), dist_rgb->pixels.size());
    if (!result) {
        JxlButteraugliApiDestroy(api);
        if (runner)
            JxlThreadParallelRunnerDestroy(runner);
        return std::nullopt;
    }

    const float distance = JxlButteraugliResultGetDistance(result, 3.0f);
    const float max_distance = JxlButteraugliResultGetMaxDistance(result);

    JxlButteraugliResultDestroy(result);
    JxlButteraugliApiDestroy(api);
    if (runner)
        JxlThreadParallelRunnerDestroy(runner);

    return std::vector<Score>{
        {"Butteraugli (3-norm)", distance},
        {"Butteraugli (max)",    max_distance},
    };
}
