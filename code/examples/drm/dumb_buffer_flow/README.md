
# dumb_buffer_flow  
  
本章的目的：  
  
> 建立「userspace framebuffer 如何進入 DRM pipeline」的完整心智模型。  
  
---  
  
## 🎯 本章的目的  
  
理解：  
  
- dumb buffer 是什麼  
- DRM framebuffer 如何建立  
- mmap 為什麼會出現在 DRM userspace  
- drmModeAddFB2() 在做什麼  
- atomic commit 前實際準備了哪些東西  
  
---  
  
## 🧠 一句話先記住  
  
```text  
dumb buffer
= 最簡單、CPU 可直接存取的 framebuffer memory
```

----------

## 🔄 完整流程

```
open(/dev/dri/card0)
 ↓
DRM_IOCTL_MODE_CREATE_DUMB
 ↓
取得 buffer handle
 ↓
DRM_IOCTL_MODE_MAP_DUMB
 ↓
mmap()
 ↓
userspace 填畫面
 ↓
drmModeAddFB2()
 ↓
得到 framebuffer object
 ↓
atomic commit
 ↓
driver update()
```

----------

## 🧠 為什麼叫 dumb？

因為它：

-   沒有 GPU acceleration
-   沒有 tiling
-   沒有 compression
-   沒有 modifier

就是：

```
「一塊最普通的 linear framebuffer」
```
## 🧩 Kernel 原始碼對照

```
drivers/gpu/drm/drm_dumb_buffers.c
drivers/gpu/drm/drm_framebuffer.c
drivers/gpu/drm/drm_ioctl.c
```
