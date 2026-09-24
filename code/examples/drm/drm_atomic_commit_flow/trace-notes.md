# Kernel trace notes — drm_atomic_commit_flow

## 1. Level 1：用人話理解

以前 legacy KMS：
```text
一步一步改 display
```

例如：

```
先改 plane
再改 CRTC
再開 connector
```

中途：

```
畫面可能進入不一致狀態
```

## 2. atomic commit 的世界

modern DRM：

```
先建立完整新 state
 ↓
全部檢查
 ↓
一次切換
```

## 3. Level 2：display state 是什麼？

atomic state 包含：

### 3.1 plane state

```
plane 顯示哪張 framebuffer
```

例如：

-   FB_ID
-   SRC_X/Y
-   CRTC_X/Y

### 3.2 CRTC state

```
scanout 狀態
```

例如：

-   mode
-   active
-   vblank

### 3.3 connector state

```
輸出 routing
```

例如：

-   connector 接哪個 CRTC
-   link status

## 4. atomic commit 真正做的事情

```
建立「下一個 display 世界」
```

而不是：

```
立刻亂改硬體
```

## 5. Level 3：kernel trace（真正發生什麼）

### 5.1 userspace

```
DRM_IOCTL_MODE_ATOMIC
```

↓

kernel：

```
drm_mode_atomic_ioctl()
```

### 5.2 state allocation

```
drm_atomic_state_alloc()
```

建立：

```
struct drm_atomic_state
```

### 5.3 state check

```
drm_atomic_check_only()
```

↓

driver：

```
plane->atomic_check()
crtc->atomic_check()
```

## 6. check 在檢查什麼？

例如：

```
這張 framebuffer 能不能 scanout？
```

```
這個 plane 支不支援這個 format？
```

```
CRTC bandwidth 夠不夠？
```

### 6.1 commit

```
drm_atomic_commit()
```

↓

```
drm_atomic_helper_commit()
```

### 6.2 helper commit flow

```
disable old state
 ↓
update planes
 ↓
enable new state
```

### 6.3 最後到 driver callback

```
pipe->update()
```

driver 在這裡：

```
設定真正 hardware register
```

## 7. page flip 是什麼？

page flip：

```
scanout framebuffer 改成另一張
```

也就是：

```
plane state 的 FB_ID 改變
```

## 8. 為什麼 atomic 很重要？

因為 modern display：

-   多 plane
-   HDR
-   scaling
-   rotation
-   async update
-   multiple displays

不能再：

```
一步一步亂改
```

## 9. 最重要一句話

```
atomic commit
本質上是在切換「整個 display state」
```

## 10. 最後總結（modern DRM mental model）
```
framebuffer  
↓  
plane state  
↓  
CRTC state  
↓  
connector state  
↓  
atomic commit  
↓  
hardware update
```

## 11. userspace 對照程式

本章新增：  

```text  
userspace/atomic_modeset_minimal.c
```
這是 legacy `drmModeSetCrtc()` 的 atomic 版本。

它示範：

```
open /dev/dri/card0
 ↓
drmSetClientCap(UNIVERSAL_PLANES)
drmSetClientCap(ATOMIC)
 ↓
找 connector / CRTC / primary plane
 ↓
CREATE_DUMB
 ↓
mmap + fill framebuffer
 ↓
drmModeCreatePropertyBlob(mode)
 ↓
drmModeAtomicAddProperty()
 ↓
drmModeAtomicCommit()
```

### 11.1 atomic commit 實際設定了什麼？

#### connector

```
CRTC_ID = crtc_id
```

意思是：

```
這個 connector 要接到哪個 CRTC
```

#### CRTC

```
MODE_ID = mode blob
ACTIVE = 1
```

意思是：

```
設定顯示 timing
並啟用 scanout
```

#### plane

```
FB_ID = framebuffer
CRTC_ID = crtc_id
SRC_* = framebuffer 內的來源範圍
CRTC_* = 螢幕上的顯示範圍
```

意思是：

```
這個 plane 要把哪張 framebuffer
顯示到哪個 CRTC 上
```

### 11.2 對應 kernel flow

```
drmModeAtomicCommit()
 ↓
DRM_IOCTL_MODE_ATOMIC
 ↓
drm_mode_atomic_ioctl()
 ↓
drm_atomic_state_alloc()
 ↓
drm_atomic_check_only()
 ↓
drm_atomic_commit()
 ↓
drm_atomic_helper_commit()
 ↓
driver atomic callbacks
```

### 11.3 最重要一句話

```
atomic commit 不是單純換 framebuffer

而是一次提交：
connector state
CRTC state
plane state
```
