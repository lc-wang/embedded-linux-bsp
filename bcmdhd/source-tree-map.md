# Broadcom bcmdhd (DHD) Wi-Fi Driver — Source Tree Map & Reading Guide

本章目標是：

- 建立 `drivers/net/wireless/bcmdhd/` 的 **模組地圖**
- 清楚劃分 **cfg80211 glue / DHD core / bus layer**
- 給出一條 **實際可操作的閱讀路線**

## 1. Source Tree 高層分類

在 Android / vendor kernel 中，bcmdhd 通常集中於：
```
drivers/net/wireless/bcmdhd/
```

可以邏輯上分成 **四大區塊**：
```
bcmdhd/
├── cfg80211 glue
├── DHD core
├── Bus layer
└── Shared / utility
```

## 2. cfg80211 Glue Layer

### 2.1 主要檔案

| 檔案 | 說明 |
|---|---|
| `wl_cfg80211.c` | cfg80211 ops 的主要實作 |
| `wl_cfgp2p.c` | P2P / GO / GC 支援 |
| `wl_cfgscan.c` | scan 流程輔助 |
| `wl_cfgvendor.c` | vendor-specific NL80211 commands |

### 2.2 角色定位

- **實作 Linux 標準介面**
  - `struct cfg80211_ops`
- **不實作 MAC 邏輯**
- 將 cfg80211 操作轉為：
  - Broadcom ioctl
  - Broadcom iovar

**所有無線行為最終都變成「對 firmware 的指令」**

### 2.3 你會在這裡看到的典型內容

- `wl_cfg80211_scan()`
- `wl_cfg80211_connect()`
- `wl_cfg80211_disconnect()`
- `wl_cfg80211_add_key()`
- `wl_cfg80211_start_ap()`

**重點**  
> 這一層「描述 *要做什麼*」，不描述「*怎麼做*」。

## 3. DHD Core Layer（Driver Heart）

### 3.1 主要檔案

| 檔案 | 說明 |
|---|---|
| `dhd_linux.c` | Linux glue、netdev ops、PM |
| `dhd_common.c` | 控制通道、event framework |
| `dhd_flowring.c` | TX flow ring / flow control |
| `dhd_wlfc.c` | Wireless Flow Control（依 tree） |
| `dhd_watchdog.c` | watchdog / health check |

### 3.2 `dhd_linux.c`：第一優先閱讀檔案

`dhd_linux.c` 是 **Linux 世界的入口點**：

- module init / exit
- netdevice lifecycle
- TX/RX data path
- suspend / resume
- notifier（IPv4/IPv6、PM）

你會在這裡看到：

- `dhd_open()`
- `dhd_stop()`
- `dhd_start_xmit()`
- `dhd_rx_frame()`
- `dhd_watchdog()`

**實務建議**  
> 任何「Wi-Fi 卡住 / 沒流量 / resume 掛掉」  
> **第一個 grep 的檔案就是 `dhd_linux.c`**

### 3.3 `dhd_common.c`：控制平面核心

- ioctl / iovar 包裝
- 與 firmware 的 command protocol
- event parsing 與 dispatch

常見關鍵 API：

- `dhd_wl_ioctl()`
- `dhd_iovar()`
- `dhd_event_process()`

## 4. Bus Layer

### 4.1 Bus 對應檔案

| Bus | 檔案 |
|---|---|
| SDIO | `dhd_sdio.c`, `bcmsdh.c` |
| PCIe | `dhd_pcie.c`, `dhd_msgbuf.c` |
| USB | `dhd_usb.c` |

### 4.2 SDIO Bus（`dhd_sdio.c`）

特徵：

- CMD52 / CMD53
- block-based transfer
- aggregation 極度影響效能與穩定性

你會看到：

- SDIO interrupt handler
- RX/TX aggregation
- sleep / wakeup handshake

**SDIO 是最常見問題來源**
- resume 後卡死
- timeout
- data corruption

### 4.3 PCIe Bus（`dhd_pcie.c`）

特徵：

- DMA ring buffer
- msgbuf protocol
- doorbell interrupt

Host memory (rings) ⇄ Dongle DMA

你會看到：

- ring allocation
- DMA mapping
- completion handling

**PCIe debug 難度最高，但效能最好**

## 5. Shared / Utility Layer

### 5.1 常見輔助檔案

| 檔案 | 用途 |
|---|---|
| `wldev_common.c` | wl ioctl / iovar helper |
| `bcmevent.c` | firmware event format |
| `siutils.c` | silicon / backplane helper |
| `sbutils.c` | system bus helper |
| `dhd_dbg.h` | log 等級與 debug macro |

### 5.2 為什麼這些檔案重要？

- event format 解析常常要回來看
- 很多 magic number / flag 定義在這裡
- vendor tree 差異通常藏在 utility layer

## 6. 模組責任邊界總覽

| 問題類型 | 該看哪一層 |
|---|---|
| Scan / connect 行為異常 | `wl_cfg80211.c` |
| ioctl / iovar 失敗 | `dhd_common.c` |
| 沒流量 / throughput 低 | `dhd_linux.c` |
| TX 卡住 | `dhd_flowring.c` |
| resume 後掛死 | `dhd_sdio.c` / `dhd_pcie.c` |
| firmware 下載失敗 | bus layer + `dhd_common.c` |
