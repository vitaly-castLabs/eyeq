# eyeq

Objective image quality measurement tool.

## Prerequisites

Ubuntu / Debian:

```bash
sudo apt install build-essential clang cmake libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libfftw3-dev libhwy-dev liblcms2-dev libjpeg-dev libpng-dev pkg-config meson ninja-build nasm
```

`libvmaf` is not packaged in Ubuntu, so it's built from source into a project-local prefix:

```bash
./scripts/install-vmaf.sh
```

The script clones Netflix/vmaf at a pinned tag, builds it with Meson, and installs to `third_party/vmaf-install/` (gitignored). Run once per clone; CMake auto-detects it via `PKG_CONFIG_PATH`. If you already have libvmaf installed system-wide, skip this step.

macOS with Homebrew:

```bash
brew install cmake pkgconf ffmpeg fftw jpeg-xl highway little-cms2 jpeg-turbo libpng libvmaf
```

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Usage

```bash
./build/eyeq [options] <reference> <distorted>
```

### Options

| Flag         | Metric |
| ------------ | ------ |
| `--all`      | Enable every metric |
| `--psnr`     | Peak Signal-to-Noise Ratio, full frame (YUV 4:2:0 weighted 4:1:1) |
| `--psnr-y`   | Peak Signal-to-Noise Ratio, Y plane only |
| `--ssim`     | Structural Similarity Index (Y channel) |
| `--ms-ssim`  | Multi-Scale SSIM (Y channel) |
| `--psnr-hvs` | PSNR with Human Visual System weighting |
| `--xpsnr`    | Extended Perceptually Weighted PSNR, full frame (YUV 4:2:0 weighted 4:1:1; Fraunhofer HHI; algorithm ported from FFmpeg's `vf_xpsnr.c`) |
| `--xpsnr-y`  | Extended Perceptually Weighted PSNR, Y plane only |
| `--fsim`     | Feature Similarity Index, luminance only (Zhang et al. 2011; phase congruency + Scharr gradient) |
| `--fsimc`    | FSIM with chromatic component (YIQ I/Q channels) |
| `--mdsi`     | Mean Deviation Similarity Index (Nafchi et al. 2016; lower is better, 0 = identical) |
| `--vmaf`     | VMAF (model `vmaf_v0.6.1`) |
| `--vmaf-neg` | VMAF-NEG (model `vmaf_v0.6.1neg`; less gameable by enhancement filters like sharpening) |
| `--ssim2`, `--ssimulacra2` | SSIMULACRA 2.1 (fetched from [cloudinary/ssimulacra2](https://github.com/cloudinary/ssimulacra2) and linked in-process) |

libvmaf caps PSNR at 60 dB when planes are identical.

No flags defaults to `--psnr` only.

### Examples

Single metric (default PSNR):

```bash
./build/eyeq ref.png distorted.png
```

All metrics:

```bash
./build/eyeq --all ref.jpg distorted.jpg
```
