#pragma once

struct Ssimulacra2RecursiveGaussian {
    float n2[3] = {};
    float d1[3] = {};
    float mul_prev[3 * 4] = {};
    float mul_prev2[3 * 4] = {};
    float mul_in[3 * 4] = {};
    int radius = 0;
};

void ssimulacra2_fast_gaussian_horizontal_hwy(const Ssimulacra2RecursiveGaussian& rg, const float* in, int w, int h, int stride, float* out);
void ssimulacra2_fast_gaussian_vertical_hwy(const Ssimulacra2RecursiveGaussian& rg, const float* in, int w, int h, int stride, float* out);
