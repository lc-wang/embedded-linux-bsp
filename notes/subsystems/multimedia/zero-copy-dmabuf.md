# Zero-Copy Pipeline with DMA-BUF

> 本章目標  
> 深入理解 **DMA-BUF（DMA-BUF）** 的 buffer sharing 機制與 zero-copy pipeline

## 1. 為什麼需要 DMA-BUF？

在 multimedia pipeline 中，資料通常經過：
```
camera → ISP → codec → display
```
如果每一層都 copy：

✗ CPU copy → latency ↑  
✗ memory bandwidth ↑  
✗ power consumption ↑

解法就是：
```
DMA-BUF（zero-copy）
```

## 2. Zero-Copy Pipeline

```
Camera  
 │  
 ▼  
V4L2 driver  
 │  
 ▼  
dmabuf fd  
 │  
 ▼  
GStreamer buffer  
 │  
 ▼  
DRM framebuffer  
 │  
 ▼  
Display
```
✓ 沒有 memory copy  
✓ buffer 在 driver 間共享

## 3. DMA-BUF 核心概念

DMA-BUF 是 Linux kernel 中的：

跨 driver buffer sharing 機制

### 3.1 三個角色

| Role | 說明 |  
|-------------|---------------------|  
| exporter | 提供 buffer |  
| importer | 使用 buffer |  
| dmabuf fd | Userspace handle |

### 3.2 範例

```
V4L2 → exporter  
DRM → importer
```

## 4. Exporter / Importer Flow

```
[V4L2 driver]  
 │  
 ▼  
dma_buf_export()  
 │  
 ▼  
fd (userspace)  
 │  
 ▼  
[DRM driver]  
 │  
 ▼  
dma_buf_attach()  
dma_buf_map_attachment()
```

## 5. Userspace Flow

userspace（GStreamer / Android）只看到：
```
dmabuf fd
```
流程：
```
V4L2 → export fd  
 │  
 ▼  
GStreamer buffer  
 │  
 ▼  
kmssink → DRM import fd
```

## 6. DRM PRIME

DRM 使用 PRIME 來處理 dmabuf：
```
drmPrimeFDToHandle()
```
流程：
```
dmabuf fd  
 │  
 ▼  
GEM handle  
 │  
 ▼  
framebuffer
```

## 7. 完整 Zero-Copy Flow

```
Camera  
 │  
 ▼  
V4L2 driver  
 │  
 ▼  
dmabuf fd  
 │  
 ▼  
GStreamer buffer (no copy)  
 │  
 ▼  
kmssink  
 │  
 ▼  
DRM import (PRIME)  
 │  
 ▼  
plane scanout
```

## 8. Memory Layout

dmabuf 並不是單純 linear memory。

可能是：

| 類型 | 說明 |  
|--------|------------------------------|  
| linear | 一般 linear memory |  
| tiled | GPU optimized memory layout |  
| AFBC | Arm Frame Buffer Compression |

### 8.1 AFBC

AFBC 特性：
```
compressed  
tile-based  
GPU-friendly
```
問題：

✗ DRM driver 不一定支援  
✗ kmssink 可能無法使用

## 9. Cache Coherency

CPU / device 共享 memory：
```
CPU cache  
Device memory
```
需要：
```
dma_sync_*
```
否則：

畫面錯誤 / tearing / stale data

## 10. Scatter-Gather

dmabuf buffer 可能是：
```
non-contiguous memory
```
透過：
```
sg_table
```
管理。

driver 需要支援：
```
dma_map_sg
```

## 11. Android Pipeline

Android pipeline：
```
SurfaceFlinger  
 │  
 ▼  
gralloc  
 │  
 ▼  
GraphicBuffer  
 │  
 ▼  
dmabuf fd  
 │  
 ▼  
drm_hwcomposer  
 │  
 ▼  
DRM
```

## 12. 常見問題與排查（BSP 常見問題）

### 12.1 drmPrimeFDToHandle fail

原因：
```
format / modifier 不支援
```

### 12.2 畫面亂掉

原因：
```
cache 沒 sync  
stride 錯誤
```

### 12.3 zero-copy 失敗

原因：
```
buffer 不是 dmabuf  
或 modifier 不匹配
```

### 12.4 Android 強制 AFBC

原因：
```
gralloc 預設開 AFBC
```

## 13. Debug 技巧

### 13.1 查看 buffer modifier（Android）

```
adb shell dumpsys SurfaceFlinger
```

### 13.2 查看 DRM plane 支援

```
modetest -p
```

### 13.3 查看 dmabuf

```
ls /sys/kernel/debug/dma_buf/
```

### 13.4 kernel log

```
dmesg | grep dma
```

## 14. BSP Debug 思維

當畫面有問題：

### 14.1 ① buffer type

```
是不是 dmabuf？
```

### 14.2 ② format

```
NV12 / RGB 是否一致？
```

### 14.3 ③ modifier

```
linear / AFBC 是否匹配？
```

### 14.4 ④ importer

```
DRM driver 是否支援？
```
