# CynLr Line-Scan Camera Pipeline

> **Real-time industrial defect detection** — simulates a factory line-scan camera scanning a surface (cloth/paper roll) row by row, applying Gaussian filter convolution and binary thresholding to detect defects. Two parallel threads communicate through a thread-safe bounded queue. Supports **live USB webcam**, CSV test mode, and RNG mode. Optional **NVIDIA CUDA GPU acceleration** for parallel pixel processing.

---

## Demo

### Pipeline Running Live on Terminal
![Terminal Output](docs/media/terminal_output.png)

### What the USB Webcam Captured (Notebook Page)
![Webcam Frame](docs/media/webcam_frame.png)

### Defect Detection Inference Report
![Inference Report](docs/media/defect_result.png)

### Screen Recording — Full Demo
[![Demo Video](docs/media/webcam_frame.png)](docs/media/demo_video.mp4)
> *Click image to watch `docs/media/demo_video.mp4`*

---

## Hardware Used in This Project

| Component | Details |
|---|---|
| **Laptop** | Lenovo IdeaPad 330-15IKB |
| **OS** | Ubuntu 22.04 LTS |
| **CPU** | Intel Core (UHD Graphics 620 integrated) |
| **GPU** | NVIDIA GeForce MX150 — 2GB VRAM, CUDA 13.0 |
| **Driver** | NVIDIA 580.126.09 |
| **Webcam** | External USB — IMC Networks `/dev/video2` |
| **OpenCV** | 4.5.4 |

### GPU Specs — NVIDIA GeForce MX150
```
+--------------------------------------------------+
|  NVIDIA GeForce MX150                            |
|  VRAM       : 2048 MiB (2 GB GDDR5)             |
|  CUDA Cores : 384 (Pascal architecture, SM 6.1)  |
|  CUDA Ver.  : 13.0  |  Driver: 580.126.09        |
|  TDP        : 11W  (low-power laptop GPU)        |
|  Compute    : 6.1                                |
+--------------------------------------------------+
```

### What the MX150 Can Do for This Pipeline

| Task | CPU Only | MX150 GPU |
|---|---|---|
| 640×480 frame (307,200 pixels) | ~5–10 ms | ~0.5–1 ms |
| Gaussian convolution (9-tap) | Sequential loop | 384 cores in parallel |
| Max throughput | ~100K pixels/sec | ~1M+ pixels/sec |
| Memory bandwidth | Shared RAM | Dedicated 2GB GDDR5 |

> **Important:** MX150 is a low-power laptop GPU (11W TDP, 2GB VRAM).  
> It gives **~5–10× speedup** over CPU for this workload.  
> See below for what a better GPU would give you.

---

## What If You Use a Better GPU?

| GPU | CUDA Cores | VRAM | Speedup vs CPU | Notes |
|---|---|---|---|---|
| **MX150 (this project)** | 384 | 2 GB | ~5–10× | Low-power laptop GPU |
| GTX 1650 | 896 | 4 GB | ~20× | Entry desktop/laptop |
| RTX 3050 | 2048 | 4 GB | ~50× | Mid-range |
| RTX 3060 | 3584 | 12 GB | ~80× | Good for real-time 4K |
| RTX 4090 | 16384 | 24 GB | ~300× | Process multiple cameras simultaneously |
| A100 (datacenter) | 6912 | 80 GB | ~500× | Process 100+ camera streams |

> With an **RTX 4090**, this pipeline could process a 4K (3840×2160) frame in real-time — that's 8.3 million pixels processed simultaneously every frame.

### Why More VRAM Matters

```
MX150 (2GB):
  Max batch size : ~500,000 pixels (fits in VRAM)
  Max frame size : ~700×700 pixels per batch

RTX 3060 (12GB):
  Max batch size : ~3,000,000 pixels
  Max frame size : ~1730×1730 per batch (full HD+)

RTX 4090 (24GB):
  Max batch size : ~6,000,000 pixels
  Max frame size : Multiple 4K frames simultaneously
```

---

## How the Pipeline Works — Step by Step

### System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    CynLr Pipeline                               │
│                                                                 │
│  USB Webcam          Block 1                  Block 2           │
│  /dev/video2   ───►  DataGeneration    ──►   FilterThreshold   │
│  640×480 frame       Block (Thread A)         Block (Thread B)  │
│                      Every T ns:              Gaussian filter   │
│                      reads 2 pixels           + threshold       │
│                      pushes PixelPair         → 0 or 1         │
│                             │                      │            │
│                    ┌────────▼────────┐             │            │
│                    │ BoundedQueue    │             │            │
│                    │ capacity = m   │             │            │
│                    │ Thread-safe    │             │            │
│                    │ mutex + CV     │             │            │
│                    └─────────────────┘             │            │
│                                                    ▼            │
│                                         data/run_<timestamp>/  │
│                                         webcam_frame.png        │
│                                         results.csv             │
│                                         defects_only.csv        │
│                                         inference_report.txt    │
└─────────────────────────────────────────────────────────────────┘
```

---

### Block 1 — Data Generation Block

**What it does:** Simulates a line-scan camera reading 2 pixels every T nanoseconds.

```
Every T nanoseconds:
  1. sleep_until(next_tick)          ← accurate timing
  2. source->next(p1, p2)            ← read 2 pixels from webcam/CSV/RNG
  3. queue->push(PixelPair{p1,p2})   ← send to Block 2
  4. next_tick += T                  ← schedule next tick
```

**Three data sources (Strategy Pattern — swap without changing anything else):**

| Source | Class | When to use |
|---|---|---|
| USB Webcam | `WebcamDataSource` | Real data — hold notebook in front of camera |
| CSV file | `CsvDataSource` | Testing with known pixel values |
| RNG | `RngDataSource` | Benchmark / stress test |

**WebcamDataSource — how it works:**
```
1. Opens /dev/video2 with V4L2 backend
2. Warms up camera (discards black frames until brightness > 10)
3. Grabs 3 frames per tick — keeps the brightest one
4. Converts BGR → greyscale
5. Resizes to exactly m_columns wide
6. Flattens to 1D uint8 array
7. next() returns 2 pixels at a time from the flat array
8. When array exhausted → grabs next live frame automatically
```

---

### BoundedQueue — Thread-Safe Bridge

**What it does:** Connects Block 1 (producer) to Block 2 (consumer) safely across threads.

```cpp
// Block 1 (Thread A) does:
queue->push(PixelPair{p1, p2, timestamp});

// Block 2 (Thread B) does:
auto pair = queue->pop();   // blocks if empty, returns nullopt if closed
```

**Key properties:**
- **Capacity = m** (number of columns) — prevents unbounded memory growth
- **Mutex + condition variables** — only one thread touches queue at a time
- **Backpressure** — if Block 2 is slow, Block 1 sleeps (queue full)
- **Graceful shutdown** — `close()` wakes all blocked threads cleanly

---

### Block 2 — Filter & Threshold Block

**What it does:** For each pixel K, applies a 9-tap Gaussian convolution then binary threshold.

#### Step 1 — Gaussian Convolution

```
Pixel stream: ... K-4, K-3, K-2, K-1, [K], K+1, K+2, K+3, K+4 ...

Weights:      0.00025 × K-4
            + 0.00867 × K-3
            + 0.07803 × K-2
            + 0.24130 × K-1
            + 0.34376 × K      ← biggest weight (centre pixel)
            + 0.24130 × K+1
            + 0.07803 × K+2
            + 0.00867 × K+3
            + 0.00013 × K+4
            ─────────────────
            = filtered_K        ← smooth value (noise removed)

All 9 weights sum to ≈ 1.0  →  normalised (brightness preserved)
```

**Why Gaussian?** It removes random noise (salt-and-pepper) while preserving real defects. A real defect spans multiple pixels; random noise is isolated.

#### Step 2 — Binary Threshold

```
if filtered_K >= TV  →  output 1  (DEFECT found!)
if filtered_K <  TV  →  output 0  (pixel is OK)
```

#### Edge Pixels (first 4 and last 4)

```
Edge padding: repeat border value
[55, 55, 55, 55, 55, 120, 130, 140, ...]
 ↑ padded with first real pixel value
```

---

### GPU Acceleration — NVIDIA MX150

**When GPU is active**, Block 2 switches from pixel-by-pixel CPU processing to batch GPU processing:

```
CPU Path (no GPU):
  for each pixel → multiply 9 weights → sum → compare to TV
  310,000 pixels × 9 multiplications = 2.7M sequential operations

GPU Path (MX150 — 384 CUDA cores):
  Collect batch of pixels
  cudaMemcpy: CPU → GPU (batch of 1024 pixels)
  Launch kernel: 4 blocks × 256 threads
    Thread 0   processes pixel 0   ┐
    Thread 1   processes pixel 1   │ ALL AT ONCE
    ...                            │ on 384 GPU cores
    Thread 1023 processes pixel 1023┘
  cudaMemcpy: GPU → CPU (results)
```

**CUDA kernel per thread:**
```cuda
__global__ void gaussian_threshold_kernel(...) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    double sum = 0.0;
    for (int k = -4; k <= 4; ++k)
        sum += input[clamp(idx+k)] * filter_weights[k+4];
    filtered[idx] = sum;
    output[idx] = (sum >= threshold_tv) ? 1 : 0;
}
```

---

### Shutdown Flow

```
User presses Ctrl+C
    │
    ▼
signal_handler() → pipeline.request_stop()
    │
    ▼
DataGen: m_running=false → queue->close()
    │
    ▼
Filter: pop() returns nullopt → exits run loop
    │
    ▼
Tail flush (CPU) or final batch (GPU)
    │
    ▼
save_results() → data/run_<timestamp>/
    │
    ▼
print_profile() → terminal summary
```

---

## Project Structure

```
CynLr_Final/
├── include/
│   ├── IDataSource.h              Strategy interface (webcam/CSV/RNG)
│   ├── PixelPair.h                Data transfer object (p1, p2, timestamp)
│   ├── BoundedQueue.h             Thread-safe producer-consumer queue
│   ├── RngDataSource.h            Random number generator source
│   ├── CsvDataSource.h            CSV file source (test mode)
│   ├── WebcamDataSource.h         Live USB webcam (continuous frames)
│   ├── CudaFilterKernel.cuh       CUDA kernel declarations
│   ├── DataGenerationBlock.h      Block 1 header
│   ├── FilterThresholdBlock.h     Block 2 header (GPU+CPU)
│   └── Pipeline.h                 Orchestrator
├── src/
│   ├── CudaFilterKernel.cu        CUDA GPU kernel (parallel Gaussian)
│   ├── DataGenerationBlock.cpp    Block 1 — timing + data source
│   ├── FilterThresholdBlock.cpp   Block 2 — GPU batch or CPU loop
│   ├── Pipeline.cpp               Wires blocks, saves results
│   └── main.cpp                   CLI entry point + Ctrl+C handler
├── tests/
│   └── tests.cpp                  Unit tests
├── data/
│   ├── sample.csv                 Test pixel data
│   └── run_20260427_201532/       Created automatically per run
│       ├── webcam_frame.png       What camera captured
│       ├── results.csv            All pixels: raw, filtered, defect
│       ├── defects_only.csv       Only defect pixels
│       └── inference_report.txt   Full analysis + verdict
├── docs/
│   └── media/
│       ├── demo_video.mp4         Screen recording of live run
│       ├── webcam_frame.png       Camera capture sample
│       ├── terminal_output.png    Terminal screenshot
│       └── defect_result.png      Inference report screenshot
├── CMakeLists.txt                 Auto-detects CUDA
└── README.md
```

---

## Build Instructions

### Step 1 — Install dependencies
```bash
sudo apt update
sudo apt install -y build-essential cmake libopencv-dev git
```

### Step 2 — Install CUDA (you already have MX150 + CUDA 13.0)
```bash
# Verify CUDA is installed
nvcc --version
nvidia-smi

# If nvcc not found:
sudo apt install -y nvidia-cuda-toolkit
```

### Step 3 — Build
```bash
cd ~/CynLr_Final
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**Expected CMake output with your MX150:**
```
-- CUDA found: 13.0 — GPU mode ENABLED
-- GPU acceleration : TRUE
========== CynLr Build Summary ==========
  GPU acceleration : TRUE
  OpenCV version   : 4.5.4
  CUDA version     : 13.0
==========================================
```

### Step 4 — Run tests
```bash
./run_tests
```

---

## Run Commands

```bash
cd ~/CynLr_Final/build

# Webcam continuous (MX150 GPU used automatically):
./CynLr_Eval1 -m 640 -tv 128 -t 500 -webcam

# Webcam — auto-stop after 5000 pairs:
./CynLr_Eval1 -m 640 -tv 128 -t 500 -webcam -pairs 5000

# Webcam verbose (print every pixel live):
./CynLr_Eval1 -m 640 -tv 128 -t 500 -webcam -pairs 2000 -verbose

# CSV test mode:
./CynLr_Eval1 -m 8 -tv 80 -t 500 -csv ../data/sample.csv

# RNG mode:
./CynLr_Eval1 -m 8 -tv 80 -t 500 -pairs 200
```

---

## View Results

```bash
# List all runs
ls ~/CynLr_Final/data/

# Read inference report
cat ~/CynLr_Final/data/run_*/inference_report.txt

# Open webcam frame
eog ~/CynLr_Final/data/run_*/webcam_frame.png

# First 20 pixels
head -20 ~/CynLr_Final/data/run_*/results.csv

# Count defects
grep ",1$" ~/CynLr_Final/data/run_*/results.csv | wc -l

# Defects only
cat ~/CynLr_Final/data/run_*/defects_only.csv
```

### Sample inference_report.txt
```
==========================================================
   CynLr Pipeline  —  Inference Report
   Run ID : 20260427_201532
==========================================================
SOURCE         : External USB Webcam (/dev/video2)
COLUMNS (m)    : 640
THRESHOLD (TV) : 128.0
INTERVAL T     : 500 ns

── WHAT THE CAMERA SAW ──
Captured frame : data/run_20260427_201532/webcam_frame.png

── PIXEL STATISTICS ──
Min pixel value  : 0
Max pixel value  : 255
Average value    : 118.72

── DEFECT DETECTION ──
OK pixels (0)    : 221340
DEFECT    (1)    : 85860
Defect rate      : 27.93%

── TIMING ──
Worst DataGen    : 312 ns
Worst gap        : 487 ns
Throughput       : PASS

── VERDICT ──
SURFACE QUALITY: POOR (27.93%)
==========================================================
```

---

## CLI Reference

| Flag | Description | Example |
|---|---|---|
| `-m <cols>` | Pixel columns — even number | `-m 640` |
| `-tv <val>` | Binary threshold TV | `-tv 128` |
| `-t <ns>` | Process interval nanoseconds | `-t 500` |
| `-webcam` | Live USB webcam `/dev/video2` | `-webcam` |
| `-cam <n>` | Camera device index | `-cam 2` |
| `-csv <path>` | CSV test mode | `-csv ../data/sample.csv` |
| `-pairs <n>` | Stop after N pairs | `-pairs 5000` |
| `-verbose` | Print every pixel live | `-verbose` |

---

## Design Patterns

| Pattern | Where | Why |
|---|---|---|
| **Strategy** | `IDataSource` | Swap webcam/CSV/RNG without changing Block 1 |
| **Producer-Consumer** | `BoundedQueue` | Thread-safe decoupling of Block 1 and Block 2 |
| **RAII** | All destructors | Camera, GPU memory, threads auto-released |
| **Observer** | `OutputCallback` lambda | Block 2 reports results without I/O coupling |
| **Parallel Reduction** | CUDA kernel | Each GPU thread processes one pixel independently |

---

## Assignment Requirements Met

| Requirement | Status | Implementation |
|---|---|---|
| Block 1 emits 2 pixels every T ns | ✅ | `sleep_until` timing in `DataGenerationBlock` |
| Block 2 filters + thresholds | ✅ | 9-tap Gaussian + binary TV in `FilterThresholdBlock` |
| Parallel threads | ✅ | Thread A (Block 1) + Thread B (Block 2) |
| Thread-safe queue size = m | ✅ | `BoundedQueue<PixelPair>` capacity = columns |
| Memory bounded by m | ✅ | Queue never grows beyond m PixelPairs |
| CSV test mode | ✅ | `CsvDataSource` |
| RNG mode | ✅ | `RngDataSource` (Mersenne Twister) |
| Timing ≤ T between outputs | ✅ | Measured and reported in profile |
| **Webcam extension** | ✅ | `WebcamDataSource` — continuous live frames |
| **GPU acceleration** | ✅ | CUDA kernel on NVIDIA MX150 |
| **Results storage** | ✅ | Auto-saved to `data/run_<timestamp>/` |

---

## Author

**Sunkeerth**  
Robotics & Embedded Systems  
Ubuntu 22.04 LTS | NVIDIA GeForce MX150 | CUDA 13.0
