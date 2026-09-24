
# Multimedia Debug Playbook

> 本章目標  
> 建立一套 **BSP 工程師可實戰使用的 Debug SOP**

----------

# 1. Debug 的核心原則

Multimedia 問題本質上都是：

data 沒有正確流動

**切 pipeline**

----------

# 2. 三層 Debug Model
```
Userspace (GStreamer / Android)  
 │  
 ▼  
Kernel (V4L2 / DRM / DMA-BUF)  
 │  
 ▼  
Hardware (sensor / ISP / display)
```
----------

# 3. Debug Flow

Step 1: 確認 userspace 有沒有資料  
Step 2: 確認 kernel 有沒有 buffer flow  
Step 3: 確認 hardware 有沒有動

----------

# 4. Case 1：Camera 沒畫面

----------

## Step 1：確認 V4L2
```
v4l2-ctl --stream-mmap
```
如果失敗：
```
問題在 driver / hardware
```
----------

## Step 2：確認 GStreamer
```
gst-launch-1.0 v4l2src ! fakesink
```
如果失敗：
```
v4l2src 問題 / format mismatch
```
----------

## Step 3：確認 display
```
gst-launch-1.0 v4l2src ! kmssink
```
如果這裡才壞：
```
DRM 問題
```
----------

# 5. Case 2：DRM 沒畫面

----------

## Step 1：確認 connector
```
modetest -M <driver>
```
----------

## Step 2：強制 modeset
```
modetest -s <conn_id>:<mode>
```
如果這裡就失敗：
```
driver / panel / timing 問題
```
----------

## Step 3：查看 state
```
cat /sys/kernel/debug/dri/0/state
```
----------

# 6. Case 3：kmssink 沒畫面但 wayland OK

經典問題

原因：
```
compositor 幫你處理了 DRM
```
實際問題：
```
你的 DRM driver 有 bug
```
----------

# 7. Case 4：DMA-BUF / zero-copy 失敗

## 檢查 buffer type
```
是不是 dmabuf？
```
----------

## 檢查 modifier
```
AFBC / linear 是否一致？
```
----------

## 檢查 importer
```
DRM driver 支援嗎？
```
----------

# 8. Case 5：Android 黑畫面

## Step 1：確認 HWC
```
adb shell dumpsys SurfaceFlinger
```
----------

## Step 2：確認 gralloc
```
buffer modifier
```
----------

## Step 3：確認 DRM
```
dmesg | grep drm
```
----------

## 常見原因
```
AFBC 不支援  
dmabuf import fail  
plane 沒設
```
----------

# 9. 問題 → Root Cause 對照表

| 現象 | Root Cause |  
|---------------------|------------------------|  
| camera 無畫面 | V4L2 driver |  
| kmssink 無畫面 | DRM plane |  
| wayland OK kmssink 不行 | DRM driver |  
| 畫面亂掉 | format / stride |  
| 黑畫面 | dmabuf fail |  
| lag / CPU 高 | 沒有 zero-copy |

----------

# 10. Debug 工具總表

----------

## GStreamer
```
gst-launch-1.0  
gst-inspect-1.0  
GST_DEBUG=3
```
----------

## V4L2
```
v4l2-ctl --all  
v4l2-ctl --stream-mmap
```
----------

## DRM
```
modetest  
cat /sys/kernel/debug/dri/0/state
```
----------

## DMA-BUF
```
ls /sys/kernel/debug/dma_buf/
```
----------

## Kernel
```
dmesg
```
----------

# 11. Debug Checklist

## Camera
```
[ ] v4l2-ctl 可以 stream  
[ ] format 正確  
[ ] driver 有 interrupt
```
----------

## DRM
```
[ ] connector connected  
[ ] mode 設定成功  
[ ] plane 有 attach
```
----------

## DMA-BUF
```
[ ] buffer 是 dmabuf  
[ ] modifier 相容  
[ ] cache sync 正確
```
----------

# 12. Debug Flow
```
camera  
 │  
 ▼  
V4L2 OK?  
 │  
 ▼  
GStreamer OK?  
 │  
 ▼  
DRM OK?  
 │  
 ▼  
DMABUF OK?  
 │  
 ▼  
Hardware OK?
```
