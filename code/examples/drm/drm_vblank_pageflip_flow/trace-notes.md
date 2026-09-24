# Kernel trace notes — drm_vblank_pageflip_flow

## 1. Level 1：用人話理解

假設：  

```text
framebuffer A 正在顯示
```

GPU 同時：

```text
render framebuffer B
```

當：

```text
下一個 frame 時間到
```

DRM：

```text
切換 scanout framebuffer
```

這就是：

```text
page flip
```

## 2. scanout 是持續進行的

CRTC：

```text
不是一次讀完整張圖
```

而是：

```text
一行一行持續讀 framebuffer
```

例如：

```text
line 0
line 1
line 2
...
```

## 3. 這就是 tearing 的來源

如果：

```text
scanout 到一半 framebuffer 被換掉
```

可能：

```text
上半部是舊畫面
下半部是新畫面
```

## 4. Level 2：vblank 是什麼？

display timing：

```text
掃完整張 frame
 ↓
短暫空檔
 ↓
開始下一 frame
```

這個空檔：

```text
vertical blank
(vblank)
```

## 5. page flip 最安全的時間

```text
vblank
```

因為：

```text
下一 frame 還沒開始 scanout
```

## 6. atomic commit 與 page flip

atomic commit：

```text
建立新的 display state
```

但：

```text
真正 framebuffer 切換
通常等 vblank
```

## 7. Level 3：kernel trace（真正發生什麼）

### 7.1 userspace commit

```text
DRM_IOCTL_MODE_ATOMIC
```

↓

```text
drm_atomic_commit()
```

### 7.2 commit tail

```text
drm_atomic_helper_commit_tail()
```

這一步：

```text
真正開始更新硬體 state
```

### 7.3 plane update

```text
plane->atomic_update()
```

driver：

```text
更新 scanout address
```

也就是：

```text
下一 frame 要掃哪張 framebuffer
```

## 8. scanout address 是什麼？

display controller：

```text
持續從某個 memory address 讀像素
```

page flip 本質上：

```text
就是換掉這個 address
```

### 8.1 vblank interrupt

下一次：

```text
vblank IRQ
```

發生時：

```text
硬體真正切換 framebuffer
```

### 8.2 page flip event

之後 DRM：

```text
通知 userspace：
flip 完成
```

例如：

-   Wayland compositor
-   SurfaceFlinger
-   games

就知道：

```text
現在新 frame 已經真的上螢幕
```

## 9. double buffering

最常見：

```text
front buffer
back buffer
```

### 9.1 rendering flow

```text
CRTC scanout:
    front buffer

GPU rendering:
    back buffer
```

↓

```text
vblank
 ↓
swap
```

## 10. 最重要觀念

```text
rendering
與
scanout

是同時進行的兩件事
```

## 11. 為什麼 modern graphics 很複雜？

因為：

```text
GPU
display controller
compositor
applications
```

全部都要：

```text
同步 frame timing
```

## 12. 最重要一句話

```text
page flip
本質上是：
「下一 frame 要掃哪張 framebuffer」
```

## 13. 最後總結（display timing mental model）
```text
GPU render back buffer  
↓  
atomic commit  
↓  
wait vblank  
↓  
flip scanout address  
↓  
new frame visible
```

## 14. userspace 對照程式

本章新增：  

```text
userspace/page_flip_minimal.c
```

這是一個真正的 minimal page flip 範例。

它示範：

```text
create dumb buffer A
create dumb buffer B
 ↓
drmModeSetCrtc(buffer A)
 ↓
render buffer B
 ↓
drmModePageFlip(buffer B)
 ↓
select()
 ↓
drmHandleEvent()
 ↓
page_flip_handler()
 ↓
swap front/back buffer
```

### 14.1 front buffer / back buffer

| buffer | 意義 |  
|---------------|------|  
| front buffer | CRTC 目前正在 scanout 的 framebuffer |  
| back buffer | userspace 正在準備下一張畫面的 framebuffer |

### 14.2 page flip 的真正意義

```text
drmModePageFlip()
```

不是立刻把畫面切掉。

它是：

```text
請 DRM 在下一個合適的 vblank
把 CRTC scanout framebuffer 換成新的 fb_id
```

### 14.3 對應 kernel flow

```text
drmModePageFlip()
 ↓
DRM_IOCTL_MODE_PAGE_FLIP
 ↓
drm_mode_page_flip_ioctl()
 ↓
driver page flip / atomic helper
 ↓
vblank
 ↓
page flip complete event
 ↓
userspace page_flip_handler()
```

### 14.4 為什麼需要 drmHandleEvent()？

因為 page flip 是非同步的。

userspace 送出：

```text
drmModePageFlip()
```

之後要等 DRM event：

```text
page flip complete
```

所以需要：

```text
select()
 ↓
drmHandleEvent()
 ↓
page_flip_handler()
```

### 14.5 最重要一句話

```text
page flip = 切換 CRTC 下一個要 scanout 的 framebuffer

page flip event = 通知 userspace 這次切換已經完成
```
