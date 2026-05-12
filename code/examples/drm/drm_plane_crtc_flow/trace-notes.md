# Kernel trace notes — drm_plane_crtc_flow

# 🟢 Level 1：用人話理解

假設：

```text
framebuffer = 一張圖片
```
但：

```
圖片放在記憶體裡不代表會出現在螢幕
```

----------

# 🟢 plane 是什麼？

plane 的工作：

```
「選一張 framebuffer 來顯示」
```

你可以理解成：

```
顯示圖層
```

----------

# 🟢 CRTC 是什麼？

CRTC 的工作：

```
真正把像素送到輸出
```

它會：

```
一行一行 scanout framebuffer
```

送到：

-   LCD
-   HDMI
-   eDP
-   MIPI DSI

----------

# 🟡 Level 2：流程理解

## atomic commit 在做什麼？

atomic commit 本質上是在更新：

```
plane state
crtc state
connector state
```

----------

## 最常見的更新

```
plane framebuffer 改了
```

也就是：

```
這個 plane 現在要顯示新的 framebuffer
```

----------

# 🔥 真正重要的地方

driver 最終通常會做：

```
設定 scanout address
```

也就是：

```
告訴硬體：從哪塊 memory 開始掃描像素
```

----------

# 🔴 Level 3：kernel trace

## userspace commit

```
DRM_IOCTL_MODE_ATOMIC
```

↓

```
drm_mode_atomic_ioctl
 └─ drm_atomic_commit
```

----------

## helper commit flow

```
drm_atomic_helper_commit
 └─ drm_atomic_helper_commit_planes
```

----------

## 最後到 driver callback

```
pipe->update()
```

driver 在這裡：

-   取得 framebuffer
-   取得 GEM memory
-   設定硬體 scanout

----------

# 🧠 plane vs framebuffer

| 元件 | 本質 |  
|-------------|----------------|  
| framebuffer | 一張圖 |  
| plane | 顯示哪張圖 |  
| CRTC | 真正輸出圖 |

----------

# 🔥 scanout 是什麼？

scanout：

```
display controller
持續從 framebuffer memory 讀像素
```

例如：

```
pixel 0
pixel 1
pixel 2
...
```

然後送到 panel。

----------

# 🧠 為什麼叫 CRTC？

歷史名稱：

```
Cathode Ray Tube Controller
```

雖然現在不是 CRT，

但名稱保留下來。

# 🧠 最後收斂
```
memory  
↓  
framebuffer  
↓  
plane  
↓  
CRTC  
↓  
scanout  
↓  
connector  
↓  
display
```

# 🔧 userspace 對照程式  
  
本章新增：  
  
```text  
userspace/modeset_minimal.c
```
它示範最小 legacy KMS 顯示流程：

```
open /dev/dri/card0
 ↓
drmModeGetResources()
 ↓
找到 connected connector
 ↓
取得 mode
 ↓
取得 encoder / crtc
 ↓
CREATE_DUMB
 ↓
mmap framebuffer memory
 ↓
drmModeAddFB()
 ↓
drmModeSetCrtc()
```
🧠 這段 code 對應 DRM 元件

| code 行為 | DRM 觀念 |  
|------------------------|----------------------------------|  
| drmModeGetConnector | 找輸出端 |  
| drmModeGetEncoder | 找 connector 後面的 encoder |  
| encoder->crtc_id | 找目前綁定的 CRTC |  
| CREATE_DUMB | 建立 framebuffer memory |  
| mmap | userspace 填 pixel |  
| drmModeAddFB | memory → framebuffer object |  
| drmModeSetCrtc | framebuffer → CRTC scanout |
