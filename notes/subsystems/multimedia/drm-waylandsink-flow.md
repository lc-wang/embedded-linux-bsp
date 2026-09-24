
# DRM / Wayland Sink Flow

> 本章目標  
> 深入理解 GStreamer sink（`kmssink` / `waylandsink`）如何驅動 **DRM（Direct Rendering Manager）** 顯示流程

----------

# 1. Sink 的兩種路徑

GStreamer 顯示有兩種主要方式：
  
| Sink | 路徑 | 特性 |  
|-------------|--------------------------|------------------------------|  
| kmssink | 直接 → DRM | 無 compositor（最接近硬體） |  
| waylandsink | → Wayland → DRM | 經 compositor |

----------

# 2. kmssink Flow

## Pipeline
```
gst-launch-1.0 v4l2src ! kmssink
```
----------

## 資料流
```
Camera  
 │  
 ▼  
v4l2src  
 │  
 ▼  
GstBuffer (dmabuf)  
 │  
 ▼  
kmssink  
 │  
 ▼  
DRM framebuffer  
 │  
 ▼  
plane → crtc → connector  
 │  
 ▼  
Panel
```
----------

## kmssink 本質
```
userspace → DRM ioctl caller
```
主要操作：
```
DRM_IOCTL_MODE_ATOMIC  
DRM_IOCTL_MODE_SETPLANE
```
----------

# 3. DRM 核心元件

DRM display pipeline：
```
Framebuffer  
 │  
 ▼  
Plane  
 │  
 ▼  
CRTC  
 │  
 ▼  
Encoder  
 │  
 ▼  
Connector  
 │  
 ▼  
Panel
```
----------

## 元件說明

| Component | Role |  
|-------------|---------------------|  
| framebuffer | Pixel buffer |  
| plane | Display layer |  
| crtc | Timing controller |  
| encoder | Signal format |  
| connector | Output interface |

----------

# 4. kmssink 與 DRM Mapping

| kmssink 行為 | DRM 對應 |  
|---------------|-------------------------|  
| create fb | drmModeAddFB2 |  
| set plane | drmModeSetPlane |  
| atomic commit | DRM_IOCTL_MODE_ATOMIC |

----------

# 5. Atomic Commit Flow

kmssink 使用 atomic modeset：
```
prepare property  
 │  
 ▼  
set plane fb_id  
 │  
 ▼  
set crtc mode  
 │  
 ▼  
atomic commit
```
----------

## Atomic 流程
```
drmModeAtomicAlloc()  
drmModeAtomicAddProperty()  
drmModeAtomicCommit()
```
----------

## Kernel flow
```
userspace (kmssink)  
 │  
 ▼  
drm ioctl  
 │  
 ▼  
drm_atomic_commit()  
 │  
 ▼  
driver (vop2 / simple_pipe / your driver)  
 │  
 ▼  
hardware update
```
----------

# 6. DMABUF → DRM

kmssink 通常接收：
```
GstBuffer (dmabuf)
```
流程：
```
dmabuf fd  
 │  
 ▼  
drmPrimeFDToHandle  
 │  
 ▼  
drmModeAddFB2  
 │  
 ▼  
framebuffer
```
----------

# 7. Zero-Copy Display
```
camera → dmabuf → DRM → panel
```
沒有：

✗ CPU copy

這是 embedded display pipeline 核心。

----------

# 8. waylandsink Flow

## Pipeline
```
gst-launch-1.0 v4l2src ! waylandsink
```
----------

## 資料流
```
v4l2src  
 │  
 ▼  
GstBuffer  
 │  
 ▼  
waylandsink  
 │  
 ▼  
Wayland compositor  
 │  
 ▼  
DRM  
 │  
 ▼  
Panel
```
----------

## waylandsink 本質
```
client → Wayland protocol
```
不直接操作 DRM。

----------

# 9. Wayland 架構
```
Application (waylandsink)  
 │  
 ▼  
Wayland client API  
 │  
 ▼  
Wayland compositor (weston / kwin)  
 │  
 ▼  
DRM backend  
 │  
 ▼  
Display
```
----------

# 10. kmssink vs waylandsink

| 項目 | kmssink | waylandsink |  
|---------|--------------|--------------------|  
| 路徑 | 直接 DRM | 經 compositor |  
| latency | 低 | 較高 |  
| control | 高 | 低 |  
| debug | 容易 | 複雜 |

----------

# 11. BSP 常見問題


## kmssink 無畫面

原因：
```
plane 沒設  
connector 沒 enable  
mode 不正確
```
----------

## atomic commit fail

原因：
```
property 不完整  
format mismatch
```
----------

## 有 connector 但沒畫面

原因：
```
CRTC 沒綁定  
plane 沒 attach
```
----------

## waylandsink 有畫面但 kmssink 沒有

原因：
```
compositor 幫你處理  
但 DRM driver 有問題
```
----------

# 12. Debug 工具


## modetest
```
modetest -M <driver>
```
查看：
```
connector  
crtc  
plane
```
----------

## 強制 modeset
```
modetest -s <conn_id>:<mode>
```
----------

## 查看 DRM state
```
cat /sys/kernel/debug/dri/0/state
```
----------

## DRM debug
```
echo 0x1ff > /sys/module/drm/parameters/debug
```
----------

## GStreamer debug
```
GST_DEBUG=3 gst-launch-1.0 v4l2src ! kmssink
```
# 13. Debug 思維

當畫面不顯示：

----------

### ① buffer 層
```
有沒有送到 kmssink？
```
----------

### ② DRM 層
```
fb 有沒有建立？  
plane 有沒有設？  
atomic commit 有沒有成功？
```
----------

### ③ hardware 層

timing / clock / enable 正確？
```
