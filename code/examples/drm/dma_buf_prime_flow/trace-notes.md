
# Kernel trace notes — dma_buf_prime_flow

# 🟢 Level 1：用人話理解假設：
```text
GPU 畫好一張圖
```

如果沒有 dma-buf：

```
GPU copy 一份
↓
compositor copy 一份
↓
DRM 再 copy 一份
```

👉 非常慢。

----------

## dma-buf 的世界

真正做法：

```
同一塊 memory大家一起用
```

----------

# 🟢 dma-buf fd 是什麼？

dma-buf 可以：

```
把 memory 變成 fd
```

這樣：

-   userspace 能傳遞
-   不同 driver 能 import/export

----------

# 🟡 Level 2：流程理解


## 1️⃣ GPU driver export memory

GPU driver：

```
GEM object
 ↓
dma_buf
 ↓
fd
```

userspace 拿到：

```
dma-buf fd
```

----------

## 2️⃣ compositor 傳遞 fd

Wayland / SurfaceFlinger：

```
收到 dma-buf fd
```

↓

傳給 DRM。

----------

## 3️⃣ DRM driver import

DRM：

```
fd
 ↓
dma_buf_get()
 ↓
drm_gem_prime_import()
```

↓

得到：

```
新的 GEM object reference
```

但：

```
還是同一塊 physical memory
```

----------

# 🔥 最重要觀念

```
import 不等於 copy
```

通常只是：

```
建立新的 reference
```

----------

# 🔥 scanout flow

最後：

```
plane
 ↓
framebuffer
 ↓
imported GEM memory
 ↓
CRTC scanout
```

----------

# 🔴 Level 3：kernel trace


## export

GPU driver：

```
drm_gem_prime_export()
```

↓

```
dma_buf_export()
```

↓

建立：

```
struct dma_buf
```

----------

## userspace

拿到：

```
dma-buf fd
```

----------

## import

DRM driver：

```
drm_gem_prime_import()
```

↓

```
dma_buf_attach()
```

↓

```
dma_buf_map_attachment()
```

↓

得到：

```
scatter-gather table
```

----------

# 🧠 為什麼會有 sg_table？

因為：

```
memory 不一定 physical contiguous
```

所以：

```
dma-buf 用 sg_table 描述 memory layout
```

----------

# 🔥 真實 subsystem sharing

| Producer | Consumer |  
|---------------------|-------------------|  
| GPU | DRM scanout |  
| Camera ISP | GPU |  
| Video decoder | DRM |  
| Wayland compositor | Display |

----------

# 🧠 Android 世界

Android gralloc：

```
alloc graphic buffer
 ↓
dma-buf fd
 ↓
SurfaceFlinger
 ↓
HWC / DRM
```
