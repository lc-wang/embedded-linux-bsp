# RK3588 Pixpaper SPI DRM Driver 與 Weston 整合 Debug 與解決報告

## 1. 問題概述

### 1.1 目標

將 Pixpaper SPI DRM driver 整合至 Weston，使 e-ink panel 作為 Wayland 顯示輸出。

目標：
```text
Wayland → Weston → DRM → SPI → Pixpaper → e-ink display
```

### 1.2 問題：Weston 無法顯示於 pixpaper

啟動 Weston：
```bash
weston --backend=drm-backend.so --tty=1  --debug
```
log：
```text
using /dev/dri/card1  
DRM: head 'HDMI-A-1' found
```
觀察：

Weston 選擇 card1 (GPU)，而非 pixpaper card0。

結果：

pixpaper 無畫面

## 2. 系統環境

Platform:
```text
RK3588
```
Kernel:
```text
Linux 6.1.75-rockchip-standard
```
Weston:
```bash
weston 13.0.3
```
DRM devices:
```text
/dev/dri/card0  ← pixpaper  
/dev/dri/card1  ← rockchip VOP2 / HDMI  
/dev/dri/card2
```
確認：
```bash
ls /sys/class/drm/
```
結果：
```text
card0-SPI-1  
card1-HDMI-A-1  
card1-HDMI-A-2
```
確認 pixpaper connector 存在。

## 3. 除錯過程

### 3.1 Driver functional verification（KMS 基本功能驗證）

先確認 DRM pipeline 正常。

使用 kmscube：
```bash
kmscube -D /dev/dri/card0
```
結果：
```text
Pixpaper successfully displays test pattern
```
確認：

-   DRM device functional

-   atomic commit functional

-   framebuffer functional

-   SPI transfer functional

### 3.2 Debug Step 1 — 確認 DRM connector 狀態

指令：
```bash
cat /sys/class/drm/card0-SPI-1/status  
cat /sys/class/drm/card0-SPI-1/modes  
cat /sys/class/drm/card0-SPI-1/enabled
```
結果：
```text
status: connected  
mode: 800x480  
enabled: disabled
```
分析：

connector 正常  
但未被 compositor 使用

### 3.3 Debug Step 2 — 確認 DRM pipeline 狀態

指令：
```bash
cat /sys/kernel/debug/dri/0/state
```
結果：
```text
connector SPI-1  
crtc active=0
```
分析：

Weston 未使用 pixpaper DRM device

### 3.4 Debug Step 3 — 強制 Weston 使用 pixpaper device（第一次嘗試）

嘗試：
```text
weston \  
  --backend=drm-backend.so \  
  --drm-device=/dev/dri/card0 \  
  --renderer=pixman \  
  --tty=1 \  
  --debug
```
錯誤：
```text
ERROR: could not open DRM device '/dev/dri/card0'
```

### 3.5 Debug Step 4 — 使用 strace 分析 Weston

指令：
```text
strace -f  -o /tmp/weston.strace weston \  
  --backend=drm-backend.so \  
  --drm-device=/dev/dri/card0 \  
  --renderer=pixman \  
  --tty=1 \  
  --debug
```
關鍵輸出：
```text
faccessat("/sys/class/drm/!dev!dri!card0", F_OK) = -1 ENOENT
```
分析：

Weston 將：
```text
/dev/dri/card0
```
轉換為：
```text
!dev!dri!card0
```
並在 sysfs 尋找：
```text
/sys/class/drm/!dev!dri!card0
```
導致失敗。

根本原因：

Weston drm-device 參數需要 device name（card0）  
而非 device path（/dev/dri/card0）

## 4. Root Cause 分析（Root Cause Summary）

問題原因：

Weston 預設選擇 GPU DRM device  
而非 pixpaper DRM device

並且：

drm-device 參數使用錯誤格式

解法：
```text
--drm-device=card0
```

## 5. 解決方案

### 5.1 Debug Step 5 — 修正 drm-device 參數

使用正確指令：
```text
weston \  
  --backend=drm-backend.so \  
  --drm-device=card0 \  
  --renderer=pixman \  
  --tty=1 \  
  --debug
```
成功。

log：
```text
using card0  
DRM: head 'SPI-1' found  
Output 'SPI-1' enabled with mode 800x480
```

### 5.2 驗證結果

確認：
```bash
cat /sys/class/drm/card0-SPI-1/enabled
```
結果：
```text
enabled
```
確認 atomic commit 成功。

### 5.3 為何需要 pixman renderer

GL renderer 會選擇 GPU：
```text
card1 (Mali)
```
pixpaper 為 tiny DRM device，無 GPU acceleration。

pixman renderer 使用 CPU render：
```text
CPU → DRM framebuffer → pixpaper
```
適用於 e-ink。

### 5.4 最終成功指令
```text
weston \  
  --backend=drm-backend.so \  
  --drm-device=card0 \  
  --renderer=pixman \  
  --tty=1 \  
  --debug
```

### 5.5 最終 DRM pipeline
```text
Wayland client  
 ↓  
Weston compositor  
 ↓  
pixman renderer  
 ↓  
DRM atomic commit  
 ↓  
pixpaper DRM driver  
 ↓  
SPI transfer  
 ↓  
e-ink refresh
```

## 附錄

### A. Debug Flow Summary
```bash
# 查看 DRM devices  
ls /dev/dri/  
  
# 查看 DRM connectors  
ls /sys/class/drm/  
  
# 查看 connector status  
cat /sys/class/drm/card0-SPI-1/status  
  
# 查看 DRM state  
cat /sys/kernel/debug/dri/0/state  
  
# 測試 DRM pipeline  
kmscube -D /dev/dri/card0  
  
# 啟動 weston debug  
weston --backend=drm-backend.so --tty=1  --debug  
  
# strace 分析  
strace -f  -o /tmp/weston.strace weston ...  
  
# 修正指令  
weston --drm-device=card0 --renderer=pixman
```
