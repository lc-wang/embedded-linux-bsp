
# drm_simple_kms
本章不是完整可用的顯示驅動，  
而是用最小骨架說明：  
  
- DRM driver 如何註冊  
- `drm_simple_display_pipe` 是什麼  
- framebuffer 如何進入 display pipeline  
- atomic commit 時 driver callback 會在什麼時候被呼叫  
  
---  
  
## 🎯 本章的目的  
  
理解以下問題：  
  
- DRM/KMS 的核心元件是什麼  
- `drm_simple_display_pipe` 為什麼適合小型/simple 顯示裝置  
- framebuffer、plane、CRTC、connector 之間的關係  
- driver 在哪裡真正更新畫面

----------

## 🧩 最核心的顯示路徑

```
userspace buffer
 ↓
framebuffer
 ↓
plane
 ↓
CRTC
 ↓
connector
 ↓
display device / panel
```

----------

## 🧠 `drm_simple_display_pipe` 是什麼？

它是一個簡化版的顯示 pipeline helper。

適合這種裝置：

-   單一輸出
-   單一 plane
-   沒有複雜 overlay
-   沒有多組 CRTC / encoder / connector 拓樸

也就是：

```
「我只有一條很單純的顯示路徑」
```

----------

## 🔧 本範例做什麼？

本範例提供：

-   一個最小 `drm_device`
-   一個 `drm_simple_display_pipe`
-   一個 `drm_connector`
-   一組固定 mode
-   `enable / update / disable` callback

重點不是硬體細節，  
而是看懂：

```
atomic commit
 ↓
helper
 ↓
driver callback
```

----------

## 🧩 Kernel 原始碼對照

```
drivers/gpu/drm/drm_simple_kms_helper.c
drivers/gpu/drm/drm_atomic_helper.c
drivers/gpu/drm/drm_gem_shmem_helper.c
include/drm/drm_simple_kms_helper.h
include/drm/drm_atomic_helper.h
```
