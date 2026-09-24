# Multimedia Debug Playbook

> 本章目標  
> 建立一套 **BSP 工程師可實戰使用的 Debug SOP**

## 1. Debug 的核心原則

Multimedia 問題本質上都是：

data 沒有正確流動

**切 pipeline**

## 2. 三層 Debug Model

```text
Userspace (GStreamer / Android)  
 │  
 ▼  
Kernel (V4L2 / DRM / DMA-BUF)  
 │  
 ▼  
Hardware (sensor / ISP / display)
```

## 3. Debug Flow

Step 1: 確認 userspace 有沒有資料  
Step 2: 確認 kernel 有沒有 buffer flow  
Step 3: 確認 hardware 有沒有動

## 4. Case 1：Camera 沒畫面

### 4.1 Step 1：確認 V4L2

```bash
v4l2-ctl --stream-mmap
```
如果失敗：
```text
問題在 driver / hardware
```

### 4.2 Step 2：確認 GStreamer

```bash
gst-launch-1.0 v4l2src ! fakesink
```
如果失敗：
```text
v4l2src 問題 / format mismatch
```

### 4.3 Step 3：確認 display

```bash
gst-launch-1.0 v4l2src ! kmssink
```
如果這裡才壞：
```text
DRM 問題
```

## 5. Case 2：DRM 沒畫面

### 5.1 Step 1：確認 connector

```bash
modetest -M <driver>
```

### 5.2 Step 2：強制 modeset

```bash
modetest -s <conn_id>:<mode>
```
如果這裡就失敗：
```text
driver / panel / timing 問題
```

### 5.3 Step 3：查看 state

```bash
cat /sys/kernel/debug/dri/0/state
```

## 6. Case 3：kmssink 沒畫面但 wayland OK

經典問題

原因：
```text
compositor 幫你處理了 DRM
```
實際問題：
```text
你的 DRM driver 有 bug
```

## 7. Case 4：DMA-BUF / zero-copy 失敗

### 7.1 檢查 buffer type

```text
是不是 dmabuf？
```

### 7.2 檢查 modifier

```text
AFBC / linear 是否一致？
```

### 7.3 檢查 importer

```text
DRM driver 支援嗎？
```

## 8. Case 5：Android 黑畫面

### 8.1 Step 1：確認 HWC

```bash
adb shell dumpsys SurfaceFlinger
```

### 8.2 Step 2：確認 gralloc

```text
buffer modifier
```

### 8.3 Step 3：確認 DRM

```bash
dmesg | grep drm
```

### 8.4 常見原因

```text
AFBC 不支援  
dmabuf import fail  
plane 沒設
```

## 9. 問題 → Root Cause 對照表

| 現象 | Root Cause |  
|---------------------|------------------------|  
| camera 無畫面 | V4L2 driver |  
| kmssink 無畫面 | DRM plane |  
| wayland OK kmssink 不行 | DRM driver |  
| 畫面亂掉 | format / stride |  
| 黑畫面 | dmabuf fail |  
| lag / CPU 高 | 沒有 zero-copy |

## 10. Debug 工具總表

### 10.1 GStreamer

```bash
gst-launch-1.0  
gst-inspect-1.0  
GST_DEBUG=3
```

### 10.2 V4L2

```bash
v4l2-ctl --all  
v4l2-ctl --stream-mmap
```

### 10.3 DRM

```bash
modetest  
cat /sys/kernel/debug/dri/0/state
```

### 10.4 DMA-BUF

```bash
ls /sys/kernel/debug/dma_buf/
```

### 10.5 Kernel

```bash
dmesg
```

## 11. Debug Checklist

### 11.1 Camera

```text
[ ] v4l2-ctl 可以 stream  
[ ] format 正確  
[ ] driver 有 interrupt
```

### 11.2 DRM

```text
[ ] connector connected  
[ ] mode 設定成功  
[ ] plane 有 attach
```

### 11.3 DMA-BUF

```text
[ ] buffer 是 dmabuf  
[ ] modifier 相容  
[ ] cache sync 正確
```

## 12. Debug Flow（逐層判斷）

```text
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
