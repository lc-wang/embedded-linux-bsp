
# v4l2src Pipeline Flow

> 本章目標  
> 深入解析 `v4l2src` 的 **userspace → kernel → hardware** 資料流與 ioctl 行為

----------

# 1. v4l2src 是什麼？

`v4l2src` 是 GStreamer 中對應 **V4L2（Video4Linux2）** 的 source plugin。

作用：
```
從 /dev/videoX 取得影像資料 → 轉成 GstBuffer → 傳入 pipeline
```
----------

# 2. 基本 Pipeline
```
gst-launch-1.0 v4l2src ! kmssink
```
資料流：
```
Camera Sensor  
 │  
 ▼  
V4L2 driver  
 │  
 ▼  
/dev/video0  
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

# 3. v4l2src 與 Kernel 的關係

`v4l2src` 本質上是：
```
userspace ioctl wrapper
```
主要透過：
```
ioctl(fd, VIDIOC_*, ...)
```
與 kernel driver 溝通。

----------

# 4. Streaming Lifecycle

完整流程：
```
open device  
 │  
 ▼  
query capability  
 │  
 ▼  
set format  
 │  
 ▼  
request buffers  
 │  
 ▼  
queue buffers  
 │  
 ▼  
stream on  
 │  
 ▼  
capture loop  
 │  
 ▼  
stream off
```
----------

# 5. 詳細 ioctl Flow


## ① open()
```
fd  =  open("/dev/video0", O_RDWR);
```
----------

## ② VIDIOC_QUERYCAP

確認 device 能力：
```
VIDIOC_QUERYCAP
```
例如：
```
V4L2_CAP_VIDEO_CAPTURE  
V4L2_CAP_STREAMING
```
----------

## ③ VIDIOC_S_FMT

設定影像格式：
```
VIDIOC_S_FMT
```
例如：
```
width  = 1920  
height = 1080  
format = V4L2_PIX_FMT_NV12
```
----------

## ④ VIDIOC_REQBUFS

要求 buffer：
```
VIDIOC_REQBUFS
```
指定：
```
memory type:  
- MMAP  
- USERPTR  
- DMABUF
```
----------

## ⑤ VIDIOC_QUERYBUF（MMAP）

取得 buffer 資訊：
```
VIDIOC_QUERYBUF
```
----------

## ⑥ mmap()
```
mmap(...)
```
將 kernel buffer 映射到 userspace。

----------

## ⑦ VIDIOC_QBUF

將 buffer 放入 queue：
```
VIDIOC_QBUF
```
----------

## ⑧ VIDIOC_STREAMON

開始 streaming：
```
VIDIOC_STREAMON
```
----------

## ⑨ Capture Loop
```
while (running) {  
  VIDIOC_DQBUF  ←  dequeue  buffer（取得  frame）  
  process  buffer  
  VIDIOC_QBUF  ←  requeue  buffer  
}
```
----------

## ⑩ VIDIOC_STREAMOFF
```
VIDIOC_STREAMOFF
```
----------

# 6. Buffer Flow
```
Kernel driver allocate buffer  
 │  
 ▼  
queue (VIDIOC_QBUF)  
 │  
 ▼  
hardware fill data  
 │  
 ▼  
dequeue (VIDIOC_DQBUF)  
 │  
 ▼  
userspace (v4l2src)  
 │  
 ▼  
GstBuffer
```
----------

# 7. Memory Type


## MMAP
```
kernel allocate  
userspace mmap
```
優點：

✓ 簡單  
缺點：

✗ 需要 copy

----------

## DMABUF
```
kernel export fd  
userspace share buffer
```
優點：

✓ zero-copy  
✓ 高效能

----------

## USERPTR
```
userspace 提供 memory
```
較少用。

----------

# 8. DMABUF Flow
```
V4L2 driver  
 │  
 ▼  
dmabuf fd  
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
DRM plane
```
沒有 copy：
```
camera → display (zero-copy)
```
----------

# 9. 與 DRM 的關係

當 pipeline：
```
v4l2src ! kmssink
```
資料流：
```
V4L2 buffer → dmabuf → DRM framebuffer
```
這正是：

Embedded Linux display pipeline 核心

----------

# 10. 常見問題



## VIDIOC_DQBUF 卡住

原因：
```
driver 沒有填資料  
interrupt 沒來
```
----------

## 無法 STREAMON

原因：
```
format 不支援  
buffer 數量不足
```
----------

## 畫面破圖

原因：
```
stride / format mismatch
```
----------

## pipeline hang

原因：
```
QBUF / DQBUF 不平衡
```
----------

## 無法 zero-copy

原因：
```
沒有使用 DMABUF
```
----------

# 11. Debug 技巧


## 查看 device 能力
```
v4l2-ctl --all
```
----------

## 查看格式
```
v4l2-ctl --list-formats-ext
```
----------

## 測試 capture
```
v4l2-ctl --stream-mmap
```
----------

## GStreamer debug
```
GST_DEBUG=3 gst-launch-1.0 v4l2src ! kmssink
```
----------

## kernel log
```
dmesg | grep v4l2
```
----------

# 12. BSP Debug 思維

當 debug：
```
camera 沒畫面
```
要切三層：

----------

### ① userspace
```
v4l2src 有沒有收到 buffer？
```
----------

### ② kernel
```
VIDIOC_DQBUF 有沒有成功？
```
----------

### ③ hardware

sensor / ISP 有沒有出資料？
```
