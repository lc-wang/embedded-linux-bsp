# i.MX95 Yocto BitBake Timeout 與 I/O Stall 問題分析報告

## 1. 問題概述

在進行 **NXP i.MX95 Yocto（fsl-imx-wayland 6.12-walnascar）** 建置時，多次遇到：

### 1.1 主要症狀

1.  `bitbake` 出現持續重試： 
```bash
NOTE: No reply from server in 30s (for command ping...)
Timeout while waiting for a reply from the bitbake server
```
2.  `bitbake-server` 進程仍在跑，但 client 無法連線：
```bash
`BlockingIOError: [Errno 11] Resource temporarily unavailable` 
```
3.  外接 SSD / HDD 出現 I/O 卡住狀況
    
4.  `/dev/sdb3`、`/tmp`、SSTATE/TMPDIR 空間不足，觸發 Yocto Disk Monitor：
```bash
`ERROR: Immediately halt since the disk space monitor action is  "HALT"!` 
```

### 1.2 最後成功的關鍵修正

✓ 將 BitBake 的執行緒數量從預設降到：
```bash
BB_NUMBER_THREADS = "2"
PARALLEL_MAKE = "-j 2"
```
✓ 建置隨即 **穩定完成 Build**。

## 2. 系統環境

| 項目 | 狀態 |
|------|-------|
| Host OS | Ubuntu 22.04 |
| Yocto | fsl-imx-wayland 6.12 |
| Build Platform | i.MX95 SMARC |
| 原本 Build Disk | /dev/sdb3（外接 HDD/SSD） |
| 新 Build Disk | /mnt/yocto-nvme（本機 NVMe） |
| 問題成因 | Disk I/O 太慢 + bitbake thread 過高 |

## 3. 除錯過程（實驗結果）

| Threads | Build 結果 | 問題 |
|---------|-------------|--------|
| 8 | ✗ bitbake server timeout | I/O stall |
| 6 | ✗ 偶發 timeout | I/O 不穩定 |
| 4 | 部分任務可跑，但仍 timeout | not stable |
| 2 | ✓ 100% 完成 Build | 最佳設定 |

## 4. Root Cause 分析

本次問題可分成 **三大類原因**：

### 4.1 I/O 競爭與外接硬碟反應延遲

原本專案位於外接 NVMe/SSD（透過 USB bridge）。  
Yocto 建置時：
-   多工 thread → 大量同時 read/write 
-   外接碟無法跟上 → I/O queue 堆積
-   BitBake server 的 IPC socket 卡住 → timeout
 
這是最典型的 BitBake timeout 成因之一。

**BitBake server 不是壞掉，是硬碟反應太慢導致 client 無法連上。**

### 4.2 Yocto TMPDIR / SSTATE_DIR 大量 I/O

Yocto 需要：

-   大量小檔案 read/write
-   比較、checksum
-   expand tarballs
-   產生 sysroot staging
-   將編譯結果同步進 tmp/work 與 sstate
    
**這些是最不適合放在外接 HDD/SSD（尤其是 USB 3.x 接口）的位置**  
→ 會造成非常明顯的 I/O stall。

### 4.3 Disk Monitor 啟動強制 HALT

遇到的訊息：
```bash
ERROR: Immediately halt since the disk space monitor action is "HALT"!
```
代表：

-   `/tmp` 在 /dev/sdb3（外接硬碟）
-   只剩不到 100MB 
-   Yocto _直接禁止_ 任何任務執行
    
這會導致 bitbake server 鎖住 socket 甚至崩潰。

### 4.4 核心結論（最關鍵一點）

> **BitBake 並不是壞掉，而是 I/O 無法支撐 8 thread 以上的高並發。**  
> 調整為 **2 threads** 後，所有 timeout 問題自然消失。

## 5. 解決方案（解決過程與設定調整）

### 5.1 將 Yocto 項目搬到 NVMe 并重建 TMPDIR

重新定位 Build 目錄：
```bash
/mnt/yocto-nvme/imx-yocto-walnascar/
```
清掉舊 TMPDIR：
```bash
rm -rf tmp
```
Yocto 自動更新了 bblayers.conf，成功啟動 Build。

### 5.2 降低 BitBake Threads（成功關鍵）

修改 local.conf：
```bash
BB_NUMBER_THREADS = "2"
PARALLEL_MAKE = "-j 2"
```
> 說明：  
> 原本 8+ threads 會讓 bitbake-server 因 I/O 反應太慢而被卡住。

**降低 thread → 外接 SSD 反應來得及 → build 正常完成**

### 5.3 調整 TMPDIR / SSTATE_DIR

使用更快的 NVMe 儲存：
```bash
TMPDIR ?= "/mnt/yocto-nvme/tmp-imx95"
SSTATE_DIR ?= "/mnt/yocto-nvme/sstate-imx95"
```
避免 I/O 過載在外接 USB 磁碟。

## 6. 結論與建議（最終建議）

### 6.1 Yocto 放 NVMe
### 6.2 TMPDIR / SSTATE_DIR 放 NVMe
### 6.3 外接磁碟僅用於 source 勿用於 tmp/sstate
### 6.4 thread 設為
```bash
BB_NUMBER_THREADS = "2"
PARALLEL_MAKE = "-j 2"
```
