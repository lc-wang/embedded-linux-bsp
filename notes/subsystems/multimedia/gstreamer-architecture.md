
# GStreamer Architecture

> 本章目標  
> 理解 **GStreamer 的核心架構與資料流模型**

GStreamer 是 Linux multimedia stack 中最常見的 userspace framework，用於：

-   camera pipeline
    
-   video playback
    
-   hardware codec
    
-   multimedia streaming
    

在 Embedded Linux BSP 開發中，GStreamer 經常作為：

userspace multimedia control layer

----------

# 1. GStreamer 核心概念

GStreamer 由以下核心概念組成：
  
| Component | Role 說明 |  
|-----------|----------------------------------|  
| Pipeline | 整個資料處理流程（data flow） |  
| Element | 處理資料的功能元件 |  
| Pad | Element 的 input / output 端點 |  
| Buffer | 在 pipeline 中傳遞的實際資料 |  
| Caps | 描述資料格式（data format） |  
| Bus | 用於 event / message communication |

----------

# 2. GStreamer Pipeline

GStreamer 使用 **pipeline 模型**來處理 multimedia data。

範例：
```
gst-launch-1.0 v4l2src ! videoconvert ! waylandsink
```
pipeline：
```
v4l2src → videoconvert → waylandsink
```
資料流：
```
Camera  
 │  
 ▼  
v4l2src  
 │  
 ▼  
videoconvert  
 │  
 ▼  
waylandsink  
 │  
 ▼  
Display
```
----------

# 3. Pipeline 架構
```
Application  
 │  
 ▼  
┌─────────────────────────────┐  
│          Pipeline           │  
│                             │  
│  element → element → element│  
│                             │  
└─────────────────────────────┘
```
每個 element 負責：

-   產生資料
    
-   轉換資料
    
-   消費資料
    

----------

# 4. Element

Element 是 GStreamer 的基本處理單位。

類型分為三種：

| Element Type | 說明 |  
|---------------|----------------|  
| Source | 產生資料 |  
| Filter | 處理資料 |  
| Sink | 消費資料 |

----------

## Source Element

範例：
```
v4l2src  
filesrc  
audiotestsrc
```
功能：
```
產生 buffer
```
例如：
```
v4l2src → 從 camera driver 取得 frame
```
----------

## Filter Element

範例：
```
videoconvert  
videoscale  
videorate
```
功能：
```
修改 buffer
```
例如：
```
RGB → YUV  
resolution change  
frame rate change
```
----------

## Sink Element

範例：
```
waylandsink  
kmssink  
filesink  
autovideosink
```
功能：
```
消費 buffer
```
例如：
```
顯示畫面  
寫入檔案
```
----------

# 5. Pad

Pad 是 element 的 **連接點**。

element 透過 pad 傳遞資料。

Pad 分為兩種：

| Pad Type | Role 說明 |  
|------------|-----------|  
| Source pad | Output |  
| Sink pad | Input |

----------

## Pad 架構
```
element A            element B  
  
src pad  ───────►  sink pad
```
資料流：
```
buffer  
 │  
 ▼  
src pad  
 │  
 ▼  
sink pad
```
----------

# 6. Caps（Capabilities）

Caps 描述 buffer 的 **格式資訊**。

例如：
```
video/x-raw
```
包含：
```
format  
width  
height  
framerate
```
範例：
```
video/x-raw,format=NV12,width=1920,height=1080
```
Caps negotiation：
```
element A → element B
```
需要 format compatible。

----------

# 7. Buffer

Buffer 是 pipeline 中傳遞的資料。

內容：
```
video frame  
audio frame  
metadata  
timestamp
```
資料流：
```
element  
 │  
 ▼  
GstBuffer  
 │  
 ▼  
next element
```
----------

# 8. Memory 管理

Buffer 內部包含 memory object：

GstMemory

常見 memory type：


  
| Memory Type | 用途說明 |  
|---------------|-----------------|  
| System Memory | CPU 一般記憶體 |  
| DMABUF | Zero-copy buffer sharing |  
| GL Memory | GPU 使用的記憶體 |

embedded pipeline 通常使用：
```
DMABUF
```
以避免 copy。

----------

# 9. Pipeline 狀態機（State Machine）

每個 pipeline 有四種狀態：
  
| State | 說明 |  
|---------|--------------------------|  
| NULL | 未初始化（pipeline 尚未建立） |  
| READY | Element 已初始化 |  
| PAUSED | Pipeline 已準備好播放 |  
| PLAYING | Pipeline 正在執行 |

----------

## State Flow
```
NULL  
 │  
 ▼  
READY  
 │  
 ▼  
PAUSED  
 │  
 ▼  
PLAYING
```
----------

# 10. Data Flow（Push Model）

GStreamer 預設使用 **push model**：
```
source element push buffer
```
資料流：
```
source  
 │  
 ▼  
buffer  
 │  
 ▼  
filter  
 │  
 ▼  
sink
```
----------

# 11. 與 Kernel Subsystem 的關係

常見 plugin 與 kernel mapping：

| Plugin | Kernel Subsystem |  
|--------------|-----------------------|  
| v4l2src | V4L2 |  
| v4l2h264dec | V4L2 M2M |  
| kmssink | DRM/KMS |  
| waylandsink | Wayland + DRM |

----------

# 12. Embedded Linux 常見 Pipeline

----------

## Camera preview

gst-launch-1.0 v4l2src ! kmssink

----------

## Camera preview（Wayland）
```
gst-launch-1.0 v4l2src ! videoconvert ! waylandsink
```
----------

## Video playback
```
gst-launch-1.0 filesrc location=test.mp4 ! decodebin ! waylandsink
```
----------

## Hardware decode
```
gst-launch-1.0 filesrc ! h264parse ! v4l2h264dec ! kmssink
```
----------

# 13. BSP Debug 時常見問題

GStreamer 問題通常不是 GStreamer 本身。

常見 root cause：


| 問題 | Root Cause 說明 |  
|----------------|------------------------------------|  
| no video | DRM plane / display pipeline 問題 |  
| frame drop | buffer queue 管理異常 |  
| pipeline hang | driver block / kernel pipeline 卡住 |  
| wrong format | caps negotiation 失敗 |

----------

# 本章總結

GStreamer 核心架構：
```
Pipeline  
 │  
Element  
 │  
Pad  
 │  
Buffer  
 │  
Caps
```
資料流：
```
Source → Filter → Sink
```
