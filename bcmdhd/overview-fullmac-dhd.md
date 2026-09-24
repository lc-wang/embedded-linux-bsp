# Broadcom bcmdhd (DHD) Wi-Fi Driver — FullMAC Architecture Overview

`bcmdhd` 是 Broadcom / Cypress Wi-Fi 晶片在 **Android / vendor kernel tree** 中最常見的驅動形式，  
其核心設計並非 Linux upstream 常見的 `mac80211`（SoftMAC），而是 **FullMAC（Dongle-based）架構**。

在 FullMAC 架構中：

- **MAC / MLME / Roaming / Scan / Rate control**  
  全部執行在 **Wi-Fi dongle firmware**
- **Linux driver（bcmdhd）**  
  只負責「控制通道 + 資料搬運 + cfg80211 glue」

這個根本差異，導致 bcmdhd 在：
- 架構
- debug 手法
- 問題定位方式  

都與 mac80211 driver **完全不同**。

## 1. FullMAC vs SoftMAC：關鍵心智模型差異

### 1.1 SoftMAC（mac80211）的特徵

| 項目 | SoftMAC |
|----|----|
| MAC state machine | Host（Linux） |
| Scan / Auth / Assoc | Host |
| Rate control | Host |
| Firmware | 極薄（PHY control） |
| Debug | trace + cfg80211/mac80211 |

Linux 掌握 **完整無線狀態**

### 1.2 FullMAC（DHD）的特徵

| 項目 | FullMAC (bcmdhd) |
|----|----|
| MAC state machine | Dongle firmware |
| Scan / Auth / Assoc | Firmware |
| Roaming | Firmware |
| Rate control | Firmware |
| Linux driver | Control + Data path |

**Linux 並不知道 Wi-Fi 真正怎麼運作，只是在「下指令 + 收事件」**

## 2. DHD（Dongle Host Driver）的整體分層
```
+-----------------------------+
| cfg80211 |
| (Linux wireless framework) |
+-------------+---------------+
|
v
+-----------------------------+
| wl_cfg80211.c |
| cfg80211 glue / policy |
+-------------+---------------+
|
v
+-----------------------------+
| dhd_linux.c |
| DHD core (netdev / PM) |
+-------------+---------------+
|
v
+-----------------------------+
| dhd_sdio / dhd_pcie |
| dhd_usb |
| (Bus layer) |
+-------------+---------------+
|
v
+-----------------------------+
| Broadcom Wi-Fi Firmware |
| (MAC / MLME / Roam) |
+-----------------------------+
```

## 3. bcmdhd 的三個核心角色

### 3.1 cfg80211 glue（`wl_cfg80211.c`）

- 實作 `struct cfg80211_ops`
- 將 Linux 標準操作轉換為 **Broadcom 私有 ioctl / iovar**
- 完全不實作 MAC 邏輯

**範例操作：**
- `scan`
- `connect`
- `disconnect`
- `set_key`
- `start_ap`

### 3.2 DHD Core（`dhd_linux.c`, `dhd_common.c`）

- netdevice lifecycle
- TX/RX data path
- event dispatch
- flow control
- watchdog / recovery
- power management glue

**這裡是 driver 的「心臟」**

### 3.3 Bus Layer（SDIO / PCIe / USB）

- 與實體硬體強烈耦合
- 決定效能、穩定性、debug 難度

| Bus | 特性 |
|---|---|
| SDIO | transaction-based、aggregation 敏感 |
| PCIe | DMA ring、msgbuf protocol |
| USB | bulk transfer、latency 高 |

## 4. 核心資料結構（閱讀 code 的鑰匙）

### 4.1 `dhd_pub_t` — DHD 公共核心狀態

```c
typedef struct dhd_pub {
    struct dhd_bus *bus;
    bool up;
    bool txoff;
    int unit;
    uint8 mac[ETHER_ADDR_LEN];
    ...
} dhd_pub_t;
```

-   與 bus 無關
    
-   control path / data path 共用
    
-   幾乎所有 dhd_* API 都會傳遞它

### 4.2 `dhd_info_t` — Linux glue 層
```
typedef struct dhd_info {
    dhd_pub_t pub;
    struct net_device *net;
    struct wireless_dev *wdev;
    struct workqueue_struct *dhd_wq;
    ...
} dhd_info_t;
```
-   netdev
    
-   wiphy
    
-   workqueue
    
-   notifier
    
**`dhd_info_t` = Linux 世界的入口**

### 4.3 `wl_cfg80211_info` — cfg80211 狀態機

-   scan state
    
-   connect state
    
-   event handling context
    
-   mutex / completion
    
## 5. Control Plane 與 Data Plane 的根本分離

### 5.1 Control Plane（命令 / 事件）

-   ioctl
    
-   iovar
    
-   firmware event
    
**bcmdhd ≠ 邏輯執行者，只是 command transporter**

### 5.2 Data Plane（封包流）

-   TX：Host Dongle
    
-   RX：Dongle Host
    
-   Flow control 完全受 firmware 回報影響
