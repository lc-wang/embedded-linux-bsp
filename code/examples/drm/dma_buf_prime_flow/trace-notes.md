# Kernel trace notes — dma_buf_prime_flow

## 1. Level 1：用人話理解

假設：
```text
GPU 畫好一張圖
```

如果沒有 dma-buf：

```text
GPU copy 一份
↓
compositor copy 一份
↓
DRM 再 copy 一份
```

非常慢。

### 1.1 dma-buf 的世界

真正做法：

```text
同一塊 memory大家一起用
```

## 2. dma-buf fd 是什麼？

dma-buf 可以：

```text
把 memory 變成 fd
```

這樣：

-   userspace 能傳遞
-   不同 driver 能 import/export

## 3. Level 2：流程理解

### 3.1 GPU driver export memory

GPU driver：

```text
GEM object
 ↓
dma_buf
 ↓
fd
```

userspace 拿到：

```text
dma-buf fd
```

### 3.2 compositor 傳遞 fd

Wayland / SurfaceFlinger：

```text
收到 dma-buf fd
```

↓

傳給 DRM。

### 3.3 DRM driver import

DRM：

```text
fd
 ↓
dma_buf_get()
 ↓
drm_gem_prime_import()
```

↓

得到：

```text
新的 GEM object reference
```

但：

```text
還是同一塊 physical memory
```

## 4. 最重要觀念

```text
import 不等於 copy
```

通常只是：

```text
建立新的 reference
```

## 5. scanout flow

最後：

```text
plane
 ↓
framebuffer
 ↓
imported GEM memory
 ↓
CRTC scanout
```

## 6. Level 3：kernel trace

### 6.1 export

GPU driver：

```text
drm_gem_prime_export()
```

↓

```text
dma_buf_export()
```

↓

建立：

```text
struct dma_buf
```

### 6.2 userspace

拿到：

```text
dma-buf fd
```

### 6.3 import

DRM driver：

```text
drm_gem_prime_import()
```

↓

```text
dma_buf_attach()
```

↓

```text
dma_buf_map_attachment()
```

↓

得到：

```text
scatter-gather table
```

## 7. 為什麼會有 sg_table？

因為：

```text
memory 不一定 physical contiguous
```

所以：

```text
dma-buf 用 sg_table 描述 memory layout
```

## 8. 真實 subsystem sharing

| Producer | Consumer |  
|---------------------|-------------------|  
| GPU | DRM scanout |  
| Camera ISP | GPU |  
| Video decoder | DRM |  
| Wayland compositor | Display |

## 9. Android 世界

Android gralloc：

```text
alloc graphic buffer
 ↓
dma-buf fd
 ↓
SurfaceFlinger
 ↓
HWC / DRM
```

## 10. userspace 對照程式
```text
userspace/prime_fd_notes.c
```

這不是完整可跑的顯示程式，而是用 code 表示 PRIME / dma-buf fd 的核心流程。

### 10.1 dma-buf fd 從哪裡來？

dma-buf fd 一定要由 producer export 出來。

常見 producer：

-   GPU driver
-   Android gralloc
-   V4L2 camera / decoder
-   Wayland client buffer

所以不能隨便 open 一個普通檔案來假裝 dma-buf。

### 10.2 userspace PRIME import flow

```text
external dma-buf fd
 ↓
drmPrimeFDToHandle()
 ↓
DRM-local GEM handle
 ↓
drmModeAddFB2()
 ↓
DRM framebuffer object
 ↓
atomic commit
 ↓
plane scanout
```

### 10.3 fd vs GEM handle

| 名稱 | 意義 |  
|-----------------|------|  
| dma-buf fd | 可跨 subsystem 傳遞的 shared memory fd |  
| GEM handle | 單一 DRM device 內部使用的 memory reference |  
| framebuffer id | DRM display pipeline 使用的顯示物件 |

### 10.4 最重要觀念

```text
drmPrimeFDToHandle()
不是 copy memory

而是：
把外部 dma-buf memory
import 成目前 DRM device 可使用的 GEM handle
```

### 10.5 完整心智模型

```text
producer memory
 ↓
dma-buf fd
 ↓
DRM import
 ↓
GEM handle
 ↓
framebuffer
 ↓
plane
 ↓
CRTC scanout
```
