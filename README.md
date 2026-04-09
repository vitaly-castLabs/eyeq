# eyeq

Objective image quality measurement tool.

## Prerequisites

```bash
sudo apt install build-essential clang cmake libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libvmaf-dev pkg-config
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

| Flag         | Metric              |
| ------------ | ------------------- |
| `--psnr`     | Peak Signal-to-Noise Ratio (Y channel) |
| `--ssim`     | Structural Similarity Index (Y channel, 8×8 sliding window) |
| `--psnr-hvs` | PSNR with Human Visual System weighting (via libvmaf) |
| `--vmaf`     | VMAF (via libvmaf)  |

No flags defaults to `--psnr` only.

### Examples

Single metric (default PSNR):

```bash
./build/eyeq ref.png distorted.png
```

All metrics:

```bash
./build/eyeq --psnr --ssim --psnr-hvs --vmaf ref.jpg distorted.jpg
```
