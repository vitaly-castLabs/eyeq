# eyeq

Objective image quality measurement tool.

## Prerequisites

```bash
sudo apt install build-essential clang cmake libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libvmaf-dev libhwy-dev liblcms2-dev libjpeg-dev libpng-dev pkg-config
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
| `--psnr-hvs` | PSNR with Human Visual System weighting |
| `--vmaf`     | VMAF |
| `--ssim2`    | SSIMULACRA 2.1 (fetched + built from [cloudinary/ssimulacra2](https://github.com/cloudinary/ssimulacra2)) |

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
