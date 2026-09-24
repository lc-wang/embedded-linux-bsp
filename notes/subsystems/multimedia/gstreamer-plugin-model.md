
# GStreamer Plugin Model

> 本章目標  
> 深入理解 **GStreamer plugin 的運作模型與 lifecycle**

GStreamer 的設計核心是 **plugin architecture**。

這使得 multimedia pipeline 可以透過不同 plugin 支援：

-   camera
    
-   codec
    
-   display
    
-   network streaming
    

----------

# 1. Plugin Architecture

GStreamer 採用 **動態 plugin 模型**：
```
GStreamer Core  
 │  
 ▼  
Plugin Loader  
 │  
 ▼  
Element (plugin implementation)
```
plugin 通常是：
```
.so shared library
```
例如：
```
libgstvideo4linux2.so  
libgstwaylandsink.so  
libgstkmssink.so
```
位置通常在：
```
/usr/lib/gstreamer-1.0/
```
----------

# 2. Plugin → Element → Pad

GStreamer 的層級：
```
Plugin  
 │  
 ▼  
Element  
 │  
 ▼  
Pad
```
解釋：


| Layer | Role |  
|--------|--------------------------------|  
| Plugin | 提供 element implementation |  
| Element| Pipeline processing unit |  
| Pad | Data flow interface |

----------

# 3. Plugin Discovery

GStreamer 啟動時會掃描 plugin。

掃描方式：
```
GST_PLUGIN_PATH  
/usr/lib/gstreamer-1.0
```
使用指令可以查看 plugin：
```
gst-inspect-1.0
```
例如：
```
gst-inspect-1.0 v4l2src
```
輸出包含：

-   element type
    
-   supported caps
    
-   pad template
    

----------

# 4. Element Lifecycle

element 在 pipeline 中會經歷 lifecycle：
```
create  
 │  
 ▼  
pad creation  
 │  
 ▼  
caps negotiation  
 │  
 ▼  
buffer allocation  
 │  
 ▼  
streaming
```
----------

# 5. Pad Template

每個 element 會定義 pad template。

例如：
```
SRC template: 'src'  
SINK template: 'sink'
```
這表示：
```
v4l2src  
 │  
 └── src pad
```
```
kmssink  
 │  
 └── sink pad
```
連接方式：
```
v4l2src.src → kmssink.sink
```
----------

# 6. Caps Negotiation

Caps negotiation 是 pipeline 成功運作的關鍵。

過程：
```
upstream element  
 │  
 ▼  
send caps  
 │  
 ▼  
downstream element  
 │  
 ▼  
accept / reject
```
例如：
```
video/x-raw  
format=NV12  
width=1920  
height=1080
```
如果 format 不相容：

pipeline 會 fail。

----------

# 7. Buffer Allocation

Buffer allocation 通常由 **sink 或 downstream element** 決定。

流程：
```
downstream propose allocation  
 │  
 ▼  
upstream allocate buffer
```
例如：
```
kmssink  
 │  
 ▼  
DRM framebuffer
```
這會影響：

memory type

----------

# 8. Memory Type

GStreamer buffer 支援多種 memory。

常見：


| Memory | 用途 |  
|---------------|-----------|  
| system memory | CPU |  
| DMABUF | Zero-copy |  
| GL memory | GPU |

embedded pipeline 常用：
```
DMABUF
```
例如：
```
camera → v4l2src → dmabuf → kmssink
```
----------

# 9. Plugin 與 Kernel Driver 的關係

GStreamer plugin 通常只是 **userspace wrapper**。

真正的工作在 kernel driver。

----------

## v4l2src

對應：
```
/dev/videoX
```
呼叫：
```
VIDIOC_REQBUFS  
VIDIOC_QBUF  
VIDIOC_DQBUF  
VIDIOC_STREAMON
```
kernel：
```
drivers/media/
```
----------

## kmssink

對應：
```
DRM device  
/dev/dri/card0
```
呼叫：
```
DRM_IOCTL_MODE_ATOMIC  
DRM_IOCTL_MODE_SETPLANE
```
kernel：
```
drivers/gpu/drm/
```
----------

## waylandsink

waylandsink 不直接控制 DRM。

流程：
```
waylandsink  
 │  
 ▼  
wayland compositor  
 │  
 ▼  
DRM
```
例如：
```
weston  
kwin
```
----------

# 10. Example Pipeline Lifecycle

以下 pipeline：
```
gst-launch-1.0 v4l2src ! kmssink
```
完整流程：
```
load plugin  
 │  
create element  
 │  
pad connect  
 │  
caps negotiation  
 │  
buffer allocation  
 │  
streaming
```
Streaming：
```
camera frame  
 │  
 ▼  
v4l2src  
 │  
 ▼  
GstBuffer  
 │  
 ▼  
kmssink  
 │  
 ▼  
DRM plane
```
----------

# 11. 常見問題

### pipeline 無法建立

原因：
```
caps negotiation fail
```
----------

### pipeline hang

原因：
```
driver block
```
例如：
```
VIDIOC_DQBUF timeout
```
----------

### 畫面沒有顯示

原因：
```
DRM plane issue
```
----------

# 12. BSP Debug 常用工具

查看 element：
```
gst-inspect-1.0
```
查看 pipeline：
```
GST_DEBUG=3 gst-launch-1.0 ...
```
查看 caps：
```
GST_DEBUG=caps:6
```
