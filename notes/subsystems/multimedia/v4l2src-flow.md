# v4l2src Pipeline Flow

> 本章目標  
> 深入解析 `v4l2src` 的 **userspace → kernel → hardware** 資料流與 ioctl 行為

## 1. v4l2src 是什麼？

`v4l2src` 是 GStreamer 中對應 **V4L2（Video4Linux2）** 的 source plugin。

作用：
```text
從 /dev/videoX 取得影像資料 → 轉成 GstBuffer → 傳入 pipeline
```

## 2. 基本 Pipeline

```bash
gst-launch-1.0 v4l2src ! kmssink
```
資料流：
```text
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

## 3. v4l2src 與 Kernel 的關係

`v4l2src` 本質上是：
```text
userspace ioctl wrapper
```
主要透過：
```text
ioctl(fd, VIDIOC_*, ...)
```
與 kernel driver 溝通。

## 4. Streaming Lifecycle

完整流程：
```text
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

## 5. 詳細 ioctl Flow

### 5.1 ① open()

```c
fd  =  open("/dev/video0", O_RDWR);
```

### 5.2 ② VIDIOC_QUERYCAP

確認 device 能力：
```text
VIDIOC_QUERYCAP
```
例如：
```text
V4L2_CAP_VIDEO_CAPTURE  
V4L2_CAP_STREAMING
```

### 5.3 ③ VIDIOC_S_FMT

設定影像格式：
```text
VIDIOC_S_FMT
```
例如：
```text
width  = 1920  
height = 1080  
format = V4L2_PIX_FMT_NV12
```

### 5.4 ④ VIDIOC_REQBUFS

要求 buffer：
```text
VIDIOC_REQBUFS
```
指定：
```text
memory type:  
- MMAP  
- USERPTR  
- DMABUF
```

### 5.5 ⑤ VIDIOC_QUERYBUF（MMAP）

取得 buffer 資訊：
```text
VIDIOC_QUERYBUF
```

### 5.6 ⑥ mmap()

```text
mmap(...)
```
將 kernel buffer 映射到 userspace。

### 5.7 ⑦ VIDIOC_QBUF

將 buffer 放入 queue：
```text
VIDIOC_QBUF
```

### 5.8 ⑧ VIDIOC_STREAMON

開始 streaming：
```text
VIDIOC_STREAMON
```

### 5.9 ⑨ Capture Loop

```text
while (running) {  
  VIDIOC_DQBUF  ←  dequeue  buffer（取得  frame）  
  process  buffer  
  VIDIOC_QBUF  ←  requeue  buffer  
}
```

### 5.10 ⑩ VIDIOC_STREAMOFF

```text
VIDIOC_STREAMOFF
```

## 6. Buffer Flow

```text
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

## 7. Memory Type

### 7.1 MMAP

```text
kernel allocate  
userspace mmap
```
優點：

✓ 簡單  
缺點：

✗ 需要 copy

### 7.2 DMABUF

```text
kernel export fd  
userspace share buffer
```
優點：

✓ zero-copy  
✓ 高效能

### 7.3 USERPTR

```text
userspace 提供 memory
```
較少用。

## 8. DMABUF Flow

```text
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
```text
camera → display (zero-copy)
```

## 9. 與 DRM 的關係

當 pipeline：
```text
v4l2src ! kmssink
```
資料流：
```text
V4L2 buffer → dmabuf → DRM framebuffer
```
這正是：

Embedded Linux display pipeline 核心

## 10. 常見問題與排查

### 10.1 VIDIOC_DQBUF 卡住

原因：
```text
driver 沒有填資料  
interrupt 沒來
```

### 10.2 無法 STREAMON

原因：
```text
format 不支援  
buffer 數量不足
```

### 10.3 畫面破圖

原因：
```text
stride / format mismatch
```

### 10.4 pipeline hang

原因：
```text
QBUF / DQBUF 不平衡
```

### 10.5 無法 zero-copy

原因：
```text
沒有使用 DMABUF
```

## 11. Debug 技巧

### 11.1 查看 device 能力

```bash
v4l2-ctl --all
```

### 11.2 查看格式

```bash
v4l2-ctl --list-formats-ext
```

### 11.3 測試 capture

```bash
v4l2-ctl --stream-mmap
```

### 11.4 GStreamer debug

```bash
GST_DEBUG=3 gst-launch-1.0 v4l2src ! kmssink
```

### 11.5 kernel log

```bash
dmesg | grep v4l2
```

## 12. BSP Debug 思維

當 debug：
```text
camera 沒畫面
```
要切三層：

### 12.1 ① userspace

```text
v4l2src 有沒有收到 buffer？
```

### 12.2 ② kernel

```text
VIDIOC_DQBUF 有沒有成功？
```

### 12.3 ③ hardware

```text
sensor / ISP 有沒有出資料？
```
