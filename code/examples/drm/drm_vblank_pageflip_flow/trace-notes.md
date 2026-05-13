# Kernel trace notes — drm_vblank_pageflip_flow
  
# 🟢 Level 1：用人話理解  
  
假設：  
  
```text  
framebuffer A 正在顯示
```

GPU 同時：

```
render framebuffer B
```

當：

```
下一個 frame 時間到
```

DRM：

```
切換 scanout framebuffer
```

這就是：

```
page flip
```

----------

# 🟢 scanout 是持續進行的

CRTC：

```
不是一次讀完整張圖
```

而是：

```
一行一行持續讀 framebuffer
```

例如：

```
line 0
line 1
line 2
...
```

----------

# 🔥 這就是 tearing 的來源

如果：

```
scanout 到一半 framebuffer 被換掉
```

可能：

```
上半部是舊畫面
下半部是新畫面
```

----------

# 🟡 Level 2：vblank 是什麼？

display timing：

```
掃完整張 frame
 ↓
短暫空檔
 ↓
開始下一 frame
```

這個空檔：

```
vertical blank
(vblank)
```

----------

# 🔥 page flip 最安全的時間

```
vblank
```

因為：

```
下一 frame 還沒開始 scanout
```

----------

# 🟡 atomic commit 與 page flip

atomic commit：

```
建立新的 display state
```

但：

```
真正 framebuffer 切換
通常等 vblank
```

----------

# 🔴 Level 3：kernel trace（真正發生什麼）

## userspace commit

```
DRM_IOCTL_MODE_ATOMIC
```

↓

```
drm_atomic_commit()
```

----------

## commit tail

```
drm_atomic_helper_commit_tail()
```

這一步：

```
真正開始更新硬體 state
```

----------

## plane update

```
plane->atomic_update()
```

driver：

```
更新 scanout address
```

也就是：

```
下一 frame 要掃哪張 framebuffer
```

----------

# 🧠 scanout address 是什麼？

display controller：

```
持續從某個 memory address 讀像素
```

page flip 本質上：

```
就是換掉這個 address
```

----------

## vblank interrupt

下一次：

```
vblank IRQ
```

發生時：

```
硬體真正切換 framebuffer
```

----------

## page flip event

之後 DRM：

```
通知 userspace：
flip 完成
```

例如：

-   Wayland compositor
-   SurfaceFlinger
-   games

就知道：

```
現在新 frame 已經真的上螢幕
```

----------

# 🧠 double buffering

最常見：

```
front buffer
back buffer
```

----------

## rendering flow

```
CRTC scanout:
    front buffer

GPU rendering:
    back buffer
```

↓

```
vblank
 ↓
swap
```

----------

# 🔥 最重要觀念

```
rendering
與
scanout

是同時進行的兩件事
```

----------

# 🧠 為什麼 modern graphics 很複雜？

因為：

```
GPU
display controller
compositor
applications
```

全部都要：

```
同步 frame timing
```

----------

# 🧠 最重要一句話

```
page flip
本質上是：
「下一 frame 要掃哪張 framebuffer」
```

# 🧠 最後總結（display timing mental model）  
```
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

# 🔧 userspace 對照程式  
  
本章新增：  
  
```text  
userspace/page_flip_minimal.c
```

這是一個真正的 minimal page flip 範例。

它示範：

```
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

----------

## 🧠 front buffer / back buffer

| buffer | 意義 |  
|---------------|------|  
| front buffer | CRTC 目前正在 scanout 的 framebuffer |  
| back buffer | userspace 正在準備下一張畫面的 framebuffer |

----------

## 🔥 page flip 的真正意義

```
drmModePageFlip()
```

不是立刻把畫面切掉。

它是：

```
請 DRM 在下一個合適的 vblank
把 CRTC scanout framebuffer 換成新的 fb_id
```

----------

## 🔴 對應 kernel flow

```
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

----------

## 🧠 為什麼需要 drmHandleEvent()？

因為 page flip 是非同步的。

userspace 送出：

```
drmModePageFlip()
```

之後要等 DRM event：

```
page flip complete
```

所以需要：

```
select()
 ↓
drmHandleEvent()
 ↓
page_flip_handler()
```

----------

## 🧠 最重要一句話

```
page flip = 切換 CRTC 下一個要 scanout 的 framebuffer

page flip event = 通知 userspace 這次切換已經完成
```
