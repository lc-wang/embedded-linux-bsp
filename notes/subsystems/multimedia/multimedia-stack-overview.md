# Multimedia Stack Overview

> 本章目標：  
> 建立 Embedded Linux Multimedia 的完整心智模型（Mental Model）

## 1. 為什麼需要理解整個 Multimedia Stack？

在 Embedded BSP 開發中，常見問題：

-   camera 有出 data 但畫面不顯示
    
-   DRM plane 沒更新
    
-   VPU decode 很慢
    
-   CPU usage 爆高
    
-   dmabuf export 失敗
    
-   Wayland 有畫面但 kmssink 沒畫面
    
這些問題的本質：

**沒有完整理解 userspace → kernel → hardware 的資料流**

## 2. 整體 Multimedia Stack 架構圖

```
┌──────────────────────────────────┐  
│            Application            │  
│  (gst-launch / Qt / Chrome etc)  │  
└──────────────────────────────────┘  
 │  
 ▼  
┌──────────────────────────────────┐  
│          GStreamer Core          │  
│   pipeline / element / buffer    │  
└──────────────────────────────────┘  
 │  
 ▼  
┌──────────────────────────────────┐  
│       GStreamer Plugins          │  
│  v4l2src / kmssink / waylandsink │  
│  v4l2h264dec / videoconvert      │  
└──────────────────────────────────┘  
 │  
 ▼  
================= Kernel Boundary =================  
 │  
 ▼  
┌──────────────────────────────────┐  
│         V4L2 Subsystem           │  
│   capture / m2m / codec driver   │  
└──────────────────────────────────┘  
 │  
┌──────────────────────────────────┐  
│         DRM Subsystem            │  
│   plane / crtc / encoder / conn  │  
└──────────────────────────────────┘  
 │  
┌──────────────────────────────────┐  
│         DMA-BUF Framework        │  
│   buffer sharing mechanism       │  
└──────────────────────────────────┘  
 │  
 ▼  
┌──────────────────────────────────┐  
│            Hardware              │  
│ Camera / ISP / VPU / Display IP │  
└──────────────────────────────────┘
```

## 3. 各層角色解析

### 3.1 Application Layer

例如：

-   `gst-launch-1.0`
    
-   Qt Multimedia
    
-   Browser
    
-   GStreamer-based player
    
負責：

-   建立 pipeline
    
-   控制播放
    
-   決定資料流方向
    
### 3.2 GStreamer Layer

核心負責：

-   buffer 管理
    
-   element 串接
    
-   caps negotiation
    
-   event 傳遞
    
例如：

gst-launch-1.0 v4l2src ! videoconvert ! waylandsink

### 3.3 Plugin Layer（實際對接 kernel）

#### v4l2src

對應 kernel：
```
/dev/videoX
```
會呼叫：
```
VIDIOC_REQBUFS  
VIDIOC_QBUF  
VIDIOC_STREAMON
```

#### kmssink / waylandsink

kmssink 直接對應：
```
DRM_IOCTL_MODE_ATOMIC
```
waylandsink 對應：
```
wayland compositor → DRM
```

### 3.4 Kernel Layer

三大 subsystem：

#### V4L2

用途：

-   camera capture
    
-   codec encode/decode
    
-   memory-to-memory device
    
source code：
```
drivers/media/
```

#### DRM

用途：

-   plane 管理
    
-   framebuffer
    
-   atomic commit
    
-   display pipeline
    
source code：
```
drivers/gpu/drm/
```

#### DMA-BUF

用途：

-   在 driver 間共享 buffer
    
-   zero-copy pipeline
    
source code：
```
drivers/dma-buf/
```

## 4. 真實資料流範例（Camera → Display）

以下是一個真實 RK / i.MX pipeline：
```
Camera Sensor  
 │  
 ▼  
ISP  
 │  
 ▼  
V4L2 Driver  
 │  
 ▼  
v4l2src (gstreamer)  
 │  
 ▼  
dmabuf export  
 │  
 ▼  
kmssink  
 │  
 ▼  
DRM Plane  
 │  
 ▼  
Panel
```
重點：

✓ 沒有 CPU copy  
✓ 使用 dmabuf  
✓ 直接 scanout

## 5. 為什麼 工程師一定要理解這層？

因為你 debug 的不是：

> GStreamer 問題

而是：

-   driver buffer 沒 dequeue
    
-   DRM plane 沒更新
    
-   dma-buf attachment fail
    
-   format 不匹配
    
-   memory type 設錯
    
## 6. Multimedia Stack 的三種典型 Pipeline

### 6.1 Capture → Display

```
v4l2src ! kmssink
```
用途：

-   camera preview
    
-   factory test
    
### 6.2 Decode → Display

```
filesrc ! h264parse ! v4l2h264dec ! kmssink
```
用途：

-   video playback
    
-   hardware decode
    
### 6.3 Encode Pipeline

```
v4l2src ! v4l2h264enc ! filesink
```
用途：

-   recording
    
-   surveillance
    
## 7. Mental Model

腦中要有這張圖：
```
Userspace element  
 ↓  
GStreamer buffer  
 ↓  
Kernel subsystem  
 ↓  
Hardware IP  
 ↓  
Back to userspace
```
只要這張圖清楚：

任何 multimedia 問題都可以定位。

## 8. 本章總結

Multimedia Stack 核心組成：

| Layer | Role 說明 |  
|--------------|------------------------------------|  
| Application | 建立與控制 multimedia pipeline |  
| GStreamer | 管理 buffer 與 pipeline flow |  
| Plugin | 作為 userspace 與 kernel 的橋樑 |  
| V4L2 | 控制 camera / video codec |  
| DRM | 控制 display 輸出 |  
| DMA-BUF | 提供跨 subsystem buffer sharing |  
| Hardware | 實際執行影像處理與輸出 |
