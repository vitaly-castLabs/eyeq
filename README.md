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
brew install cmake pkgconf ffmpeg fftw highway little-cms2 jpeg-turbo libpng libvmaf
```

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Install

To install the `eyeq` binary system-wide (default prefix `/usr/local`):

```bash
./scripts/install.sh                       # asks for sudo if prefix needs it
./scripts/install.sh --prefix ~/.local     # user-local install, no sudo
```

Or directly via cmake:

```bash
sudo cmake --install build --component eyeq
```

The `--component eyeq` flag is required — without it, the FetchContent-vendored libjxl tries to install its own files and errors out. Uninstall with `sudo rm /usr/local/bin/eyeq`.

## Usage

```bash
./build/eyeq [options] <reference> <distorted>
```

An example:
```bash
./build/eyeq --all meridian.png meridian.jpg
PSNR: 43.3639
PSNR (Y): 41.9193
SSIM: 0.990009
MS-SSIM: 0.987025
PSNR-HVS: 38.9623
XPSNR: 44.3294
XPSNR (Y): 42.9677
FSIM: 0.991833
FSIMc: 0.991704
MDSI: 0.012252
VMAF: 78.9845
VMAF-NEG: 76.9834
SSIMULACRA2: 58.2346
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
| `--dssim`    | Multi-scale L\*a\*b\* structural dissimilarity (clean-room reimplementation of Lesinski's DSSIM; lower = better, 0 = identical) |

libvmaf caps PSNR at 60 dB when planes are identical.

No flags defaults to `--psnr` only.

### Raw YUV inputs

Files with a `.yuv` extension are read as planar I420 8-bit (no header). Pass `--width` and `--height`, or pair with an image of known dimensions:

```bash
./build/eyeq --all --width 2048 --height 858 ref.yuv distorted.yuv
./build/eyeq ref.png distorted.yuv          # dimensions inherited from ref.png
```

Raw YUV has no metadata, so it's assumed to already be in the same space as everything else — BT.709, full range. If your `.yuv` uses a different matrix or range, pre-convert it (`ffmpeg -vf "scale=in_color_matrix=bt601:in_range=tv:out_color_matrix=bt709:out_range=full" -pix_fmt yuv420p -f rawvideo`). SSIMULACRA2 needs a path-backed image, so for raw YUV it materializes a temp PPM under `/tmp/` (auto-deleted).

### Examples

Single metric (default PSNR):

```bash
./build/eyeq ref.png distorted.png
```

All metrics:

```bash
./build/eyeq --all ref.jpg distorted.jpg
```
