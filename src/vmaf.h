#pragma once

#include <cstring>
#include <optional>
#include <utility>

extern "C" {
#include <libvmaf/libvmaf.h>
#include <libvmaf/model.h>
#include <libvmaf/picture.h>
}

#include "metrics.h"

// RAII wrapper managing the full VMAF lifecycle: context, model, pictures
struct VmafSession {
    VmafContext* ctx = nullptr;
    VmafModel* model = nullptr;
    VmafPicture ref_pic = {};
    VmafPicture dist_pic = {};
    bool ref_valid = false;
    bool dist_valid = false;

    VmafSession() = default;
    VmafSession(const VmafSession&) = delete;
    VmafSession& operator=(const VmafSession&) = delete;

    VmafSession(VmafSession&& o) noexcept
      : ctx(std::exchange(o.ctx, nullptr)),
        model(std::exchange(o.model, nullptr)),
        ref_pic(o.ref_pic),
        dist_pic(o.dist_pic),
        ref_valid(std::exchange(o.ref_valid, false)),
        dist_valid(std::exchange(o.dist_valid, false)) {}

    VmafSession& operator=(VmafSession&& o) noexcept {
        if (this != &o) {
            cleanup();
            ctx = std::exchange(o.ctx, nullptr);
            model = std::exchange(o.model, nullptr);
            ref_pic = o.ref_pic;
            dist_pic = o.dist_pic;
            ref_valid = std::exchange(o.ref_valid, false);
            dist_valid = std::exchange(o.dist_valid, false);
        }
        return *this;
    }

    ~VmafSession() { cleanup(); }

    bool init() {
        VmafConfiguration cfg = {};
        cfg.log_level = VMAF_LOG_LEVEL_NONE;
        cfg.n_threads = 0;
        cfg.n_subsample = 1;
        cfg.cpumask = 0;
        cfg.gpumask = 0;
        return vmaf_init(&ctx, cfg) == 0;
    }

    bool load_model() {
        VmafModelConfig cfg = {};
        cfg.name = "vmaf";
        cfg.flags = VMAF_MODEL_FLAGS_DEFAULT;
        if (vmaf_model_load(&model, &cfg, "vmaf_v0.6.1") != 0)
            return false;
        if (vmaf_use_features_from_model(ctx, model) != 0)
            return false;
        return true;
    }

    bool alloc_pictures(int w, int h) {
        if (vmaf_picture_alloc(&ref_pic, VMAF_PIX_FMT_YUV420P, 8, w, h) != 0)
            return false;
        ref_valid = true;
        if (vmaf_picture_alloc(&dist_pic, VMAF_PIX_FMT_YUV420P, 8, w, h) != 0) {
            vmaf_picture_unref(&ref_pic);
            ref_valid = false;
            return false;
        }
        dist_valid = true;
        return true;
    }

    bool use_feature(const char* name) { return vmaf_use_feature(ctx, name, nullptr) == 0; }

    std::optional<float> get_feature_score(const char* name, int index) {
        double score = 0.0;
        if (vmaf_feature_score_at_index(ctx, name, &score, index) != 0)
            return std::nullopt;
        return static_cast<float>(score);
    }

    void copy_plane_data(const Image& ref, const Image& dist) {
        for (int i = 0; i < 3; ++i) {
            const auto r = ref.plane(i);
            const auto d = dist.plane(i);
            std::memcpy(ref_pic.data[i], r.data(), r.size());
            std::memcpy(dist_pic.data[i], d.data(), d.size());
        }
    }

    bool feed_pictures(int index) {
        int ret = vmaf_read_pictures(ctx, &ref_pic, &dist_pic, index);
        if (ret == 0) {
            ref_valid = false;
            dist_valid = false;
        }
        return ret == 0;
    }

    bool flush() { return vmaf_read_pictures(ctx, nullptr, nullptr, 0) == 0; }

    std::optional<float> get_score(int index) {
        double score = 0.0;
        if (vmaf_score_at_index(ctx, model, &score, index) != 0)
            return std::nullopt;
        return static_cast<float>(score);
    }

private:
    void cleanup() {
        if (ref_valid)
            vmaf_picture_unref(&ref_pic);
        if (dist_valid)
            vmaf_picture_unref(&dist_pic);
        if (model)
            vmaf_model_destroy(model);
        if (ctx)
            vmaf_close(ctx);
    }
};

class Vmaf : public Metrics {
public:
    Vmaf(int w, int h, ColorSpace cs): Metrics(w, h, cs) {}

    const char* name() const override { return "VMAF"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override {
        if (colorspace_ != ColorSpace::I420)
            return std::nullopt;

        VmafSession runner;
        if (!runner.init())
            return std::nullopt;
        if (!runner.load_model())
            return std::nullopt;
        if (!runner.alloc_pictures(ref.width, ref.height))
            return std::nullopt;

        runner.copy_plane_data(ref, dist);
        if (!runner.feed_pictures(0))
            return std::nullopt;
        if (!runner.flush())
            return std::nullopt;
        auto score = runner.get_score(0);
        if (!score)
            return std::nullopt;
        return std::vector<Score>{{"VMAF", *score}};
    }
};
