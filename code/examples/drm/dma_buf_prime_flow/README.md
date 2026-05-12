# dma_buf_prime_flow  
  
Linux dma-buf / DRM PRIME memory sharing flow 心智模型。  
  
本章目的：  
  
> 理解不同 driver / subsystem 如何共享同一塊 framebuffer memory。  
  
---  
  
## 🎯 本章的目的  
  
理解：  
  
- dma-buf 是什麼  
- PRIME 是什麼  
- 為什麼 graphics stack 一直在 import/export memory  
- GPU / DRM / camera 如何共享同一塊 memory  
  
---  
  
## 🧠 一句話先記住  
  
```text  
dma-buf  
= Linux kernel 的共享 memory 機制
```

----------

## 🔥 最重要觀念

```
不是 copy memory

而是：
共享同一塊 memory
```

----------

## 🧠 PRIME 是什麼？

PRIME：

```
DRM 對 dma-buf sharing 的整合介面
```

也就是：

```
DRM driver
如何 import/export dma-buf
```

----------

## 🔄 真實 graphics stack

```
GPU render
 ↓
dma-buf fd
 ↓
Wayland / SurfaceFlinger
 ↓
DRM import
 ↓
scanout
```

----------

## 🧠 為什麼需要 dma-buf？

因為：

```
copy framebuffer 太貴
```

尤其：

-   4K
-   60fps
-   HDR
-   多 layer

----------

## 🔥 zero-copy 的核心

```
同一塊 physical memory
被不同 subsystem 共用
```

----------

## 🧩 Kernel 原始碼對照

```
drivers/dma-buf/
drivers/gpu/drm/drm_prime.c
drivers/gpu/drm/drm_gem_prime.c
include/linux/dma-buf.h
```
