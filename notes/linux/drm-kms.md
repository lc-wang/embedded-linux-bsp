# Linux DRM/KMS 架構與顯示驅動開發指南

本章介紹 Linux 顯示子系統（DRM / KMS）的整體架構、典型資料流、重要物件、
顯示 pipeline 組成、atomic model、framebuffer、modesetting 與顯示驅動的撰寫方式。

適用於：
- e-paper / LCD / HDMI / MIPI DSI / LVDS driver 實作  
- Rockchip / Renesas / i.MX / Allwinner display bring-up  
- TinyDRM / simpledrm / panel driver 開發  
- Android / Wayland / X11 顯示整合  

## 1. DRM 與 KMS 架構概述

DRM（Direct Rendering Manager）包含兩部分：

| 子系統 | 功能 |
| --- | --- |
| **DRM core** | 管理顯示資源、buffer、用戶層 API（ioctl） |
| **KMS (Kernel Mode Setting)** | 設定解析度、刷新率、顯示 pipeline、plane 組態 |

顯示系統 pipeline：
```text
Userspace (X11 / Wayland / Android SurfaceFlinger)
↓
libdrm
↓ ioctl
DRM core
↓
KMS driver (CRTC, Plane, Encoder, Connector)
↓
Display controller hardware
↓
Panel / HDMI / DSI / e-paper
```

## 2. KMS Pipeline 元件

KMS pipeline 是由數個物件組成的：

| 元件 | 說明 |
| --- | --- |
| **Plane** | 最底層影格來源，可以有 overlay、多層 |
| **CRTC** | Scan-out engine，負責組合 plane 並送出影像 |
| **Encoder** | 訊號編碼元件（LVDS / HDMI / DSI） |
| **Connector** | 連接實際顯示裝置（panel、HDMI Port） |
| **Mode** | 解析度 / refresh rate（timing） |

典型顯示拓撲：
```text
Plane → CRTC → Encoder → Connector → Panel
```

## 3. Atomic Mode Setting

Atomic 模式：  
所有 KMS 狀態必須一次提交（atomic commit），保證畫面無撕裂、同步。

使用方式：
```text
atomic_check() ← 驗證 state
atomic_commit() ← 實際套用設定
```

Atomic state 組成：

| State | 說明 |
| --- | --- |
| `crtc_state` | enable, mode, active |
| `plane_state` | fb, format, src/dst rect |
| `connector_state` | dpms, link status |

## 4. Framebuffer 與 GEM/SHMEM

Userspace 將影像 buffer（例如 SurfaceFlinger）傳給 kernel ：

1. 分配 dma-buf 或 SHMEM buffer  
2. mmap 給 userspace  
3. 使用 `drmModeAddFB2()` 在 kernel 中建立 FB  
4. 將 FB attach 到 plane  

DRM 常用 buffer 管理：

| 模組 | 說明 |
| --- | --- |
| **GEM** | Generic buffer 管理器 |
| **SHMEM GEM** | 使用 shmem 作後端，簡單有效 |
| **dma-buf** | 跨模組共享 buffer（GPU → VOP → Display Controller） |

## 5. Panel Driver

Panel driver 通常代表一個實體 panel（DPI、DSI、e-paper）。

典型 callback：

```c
struct drm_panel_funcs {
    .prepare
    .enable
    .disable
    .unprepare
    .get_modes
};
```

Panel driver 提供：

-   backlight 控制（可選） 
-   reset / power sequence
-   mode timing（`drm_mode_duplicate` 或固定 table）

Pixpaper driver 走：

-   spi-based panel 
-   drm_simple_display_pipe
-   polling flush mechanism
-   no continuous refresh（e-ink 特有行為）
- 

## 6. SimpleDRM / Simple Display Pipe

Tiny panel 或 e-paper 常用：

```c
drm_simple_display_pipe_init()
```

它自動建立：

-   1 個 plane    
-   1 個 CRTC 
-   連接指定 panel

適合：

-   e-paper
-   monochrome LCD
-   simple RGB panel

不適合：

-   多 plane overlay
-   GPU compositing

## 7. DRM 驅動常見結構

主結構：

```c
struct drm_driver
struct drm_device
struct drm_mode_config
struct drm_plane
struct drm_crtc
struct drm_encoder
struct drm_connector
```
常見初始化程式：

```c
drm_dev_alloc()
drm_mode_config_init()
drm_simple_display_pipe_init()
drm_dev_register()
```

## 8. 顯示時序與 Mode 設定

Mode 設定包含：

```c
hdisplay, vdisplay
hsync_start, hsync_end
htotal
vsync_start, vsync_end
vtotal
clock (pixel clock)
```
例如：

```text
800x600 @ 60Hz  → pixel clock = 40MHz
```
Panel driver 的 get_modes() 負責產生 mode。

## 9. VBLANK 與 Page Flip

顯示更新通常發生在 VBLANK（垂直消影）期間。

Userspace 呼叫：

```c
drmModePageFlip()
```
Kernel → 呼叫 CRTC 的：

```
->atomic_flush()
```
然後在下一次 VBLANK：

```c
->page_flip_finished()
```
E-paper 沒有 real-time vblank → 需自行模擬。

## 10. DRM 與 Android 整合（SurfaceFlinger）

Android 使用 HWC（Hardware Composer）與 DRM 驅動整合。

流程：

```text
SurfaceFlinger
   ↓
HWC2
   ↓
DRM backend (gralloc, drm_hwcomposer)
   ↓
DRM KMS
```

## 11. Debug 方法

### 11.1 列出 DRM 裝置

```sh
ls /sys/class/drm/
```
列出 modes
```sh
modetest -M <driver>
```
顯示 buffer 測試
```
kmscube
```
檢查 Atomic log
在 kernel config 打開：

```c
CONFIG_DRM_DEBUG
CONFIG_DRM_DEBUG_MODESET
```
可看到：

```text
atomic commit:
   plane state:
   crtc state:
   connector state:
```

## 12. 常見問題與排查

| 錯誤 | 原因 | 解法 |
|------|------|------|
| failed to enable vblank | controller 不支援 vblank | 使用 simulated vblank 或禁用 |
| no modes available | panel get_modes 未返回 mode | 修 panel driver |
| screen stuck | atomic_flush 未更新 plane fb | 填入 drm_crtc_state->event |
| image shifted | sync timing 錯誤 | 修 hsync/vsync/porch |
| flicker | pixel clock 選錯 | 計算 pixel clock 或用 panel 固定值 |
