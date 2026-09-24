# Linux WiFi Stack Overview

這份筆記整理 Linux 無線網路 (WiFi) 子系統架構，包含 cfg80211 / mac80211、nl80211、以及 user space (wpa_supplicant) 的交互流程。
目標是理解 WiFi driver 的位置與資料流。

## 1. 整體架構

```text
   +--------------------------+
   |       User Space         |
   |--------------------------|
   |  wpa_supplicant / iw     |
   |  hostapd / NetworkManager|
   +------------⇅-------------+
                ||
                ||  nl80211 (netlink)
                ⇅⇅
   +--------------------------+
   |       Kernel Space       |
   |--------------------------|
   |  cfg80211  ⇆  mac80211   |
   |     ⇅             ⇅       |
   |  vendor driver  ⇆  hw    |
   +--------------------------+
                ||
                ⇅
          [WiFi Hardware]
```
**說明：**
- `⇅` 表示雙向資料與事件傳遞。  
- **cfg80211 ⇆ nl80211 (user space)**：user space 發送命令（scan/connect），kernel 回傳事件（scan result、connect result）。  
- **cfg80211 ⇆ mac80211**：mac80211 呼叫 cfg80211 回報事件，cfg80211 呼叫 mac80211 控制行為（ex: start/stop）。  
- **mac80211 ⇆ vendor driver / hw**：驅動上報狀態、傳送封包到硬體。  

## 2. cfg80211

- **位於** `net/wireless/`。
- 提供 WiFi 子系統的統一介面，負責：
  - 與 user space 通訊 (nl80211)
  - 管理 wireless interface（AP / STA / monitor）
  - 儲存 regulatory domain、channel、SSID、BSSID 等資訊
- 驅動註冊時需呼叫：
```c
struct wiphy *wiphy = wiphy_new(&cfg80211_ops, sizeof(...));
wiphy_register(wiphy);
```
  - 提供操作 callback（cfg80211_ops）：
```c
static const struct cfg80211_ops mywifi_ops = {
    .scan = mywifi_scan,
    .connect = mywifi_connect,
    .disconnect = mywifi_disconnect,
    .set_channel = mywifi_set_channel,
};
```

## 3. nl80211

- netlink-based user space interface，定義 WiFi 命令與事件格式。

- 位於 net/wireless/nl80211.c。

- 常見命令：

  - NL80211_CMD_TRIGGER_SCAN

  - NL80211_CMD_CONNECT

  - NL80211_CMD_DISCONNECT

- 使用者工具如 iw, wpa_supplicant 透過 netlink 發送這些命令給 kernel。

範例：
```c
iw dev wlan0 scan
iw dev wlan0 connect mySSID
```

## 4. mac80211

  - **通用 802.11 軟體層框架。**

- 提供完整的 WiFi MAC 層協定邏輯 (association, authentication, management frame)。

- 驅動需透過 ieee80211_ops 註冊：
```c
static const struct ieee80211_ops mywifi_hw_ops = {
    .start = mywifi_start,
    .stop = mywifi_stop,
    .tx = mywifi_tx,
    .add_interface = mywifi_add_interface,
};

struct ieee80211_hw *hw = ieee80211_alloc_hw(sizeof(...), &mywifi_hw_ops);
ieee80211_register_hw(hw);
```
- mac80211 幫忙處理 frame aggregation、rate control、scan scheduling。

- 某些驅動（如 vendor driver）會「跳過 mac80211」直接對接 cfg80211。這類稱為 fullmac driver（如 Broadcom、Qualcomm）。
使用 mac80211 的稱為 softmac driver（如 Intel iwlwifi、Atheros ath9k）。

## 5. Fullmac vs Softmac 驅動比較

| 項目 | Fullmac Driver | Softmac Driver |
| --- | --- | --- |
| **MAC 層實作位置** | Firmware (硬體端實作 MAC 協定) | Host 端的 mac80211 處理 MAC 邏輯 |
| **驅動框架** | 直接實作 `cfg80211_ops`，與 firmware 通訊 | 使用 `mac80211` 介面，註冊 `ieee80211_ops` |
| **例子** | Broadcom、Qualcomm (ath10k)、部分 Realtek | Intel iwlwifi、Atheros ath9k |
| **優點** | CPU 負載低、開發簡單、韌體幫你做很多事 | 可控性強、開源、容易 debug |
| **缺點** | Firmware 封閉、可調性差 | 驅動較複雜、要處理 MAC 細節 |
| **典型應用** | 手機、封閉型 IoT、量產品模組 | 桌機、筆電、嵌入式 Linux、開發板 |

## 6. user space 工具

- wpa_supplicant：STA 模式下負責 WPA/WPA2 認證。

- hostapd：AP 模式的 counterpart。

- iw：直接發送 nl80211 命令，適合 debug。

- NetworkManager：高階管理層，呼叫 wpa_supplicant D-Bus API。

交互關係：

```text
iw / wpa_supplicant → nl80211 → cfg80211 → (mac80211) → driver
```

## 7. 事件流程範例：WiFi 連線

```text
User (wpa_supplicant)
   │
   │ NL80211_CMD_CONNECT
   ▼
cfg80211_ops.connect()
   │
   ▼
Driver 向 Firmware 發送 join request
   │
   ▼
Firmware 回報連線完成
   │
   ▼
Driver 呼叫 cfg80211_connect_result()
   │
   ▼
cfg80211 通知 user space 事件 (NL80211_CMD_CONNECT_RESULT)
   │
   ▼
wpa_supplicant 更新狀態 / DHCP
```

## 8. 常見 Debug 工具

| 工具 | 用途 |
| --- | --- |
| `iw` | 執行掃描、連線、斷線等基本操作 |
| `wpa_cli` | 透過 wpa_supplicant 傳送控制命令 |
| `dmesg` | 查看 kernel driver log、偵錯訊息 |
| `tcpdump -i wlan0` | 抓取無線封包，分析連線或認證過程 |
| `trace-cmd` | 追蹤 mac80211 / cfg80211 函式呼叫流程 |
| `debugfs` (`/sys/kernel/debug/ieee80211/phy0/`) | 觀察 rate control、station info、統計資訊 |

## 9. 常見問題與排查（常見 Debug case）

- 問題：無法連線
→ 檢查 dmesg 中是否有 "assoc failed"、"timeout"。
→ 確認 firmware 是否載入 (/lib/firmware/)。

- 問題：掃描不到 AP
→ 驗證 cfg80211_scan 是否有被呼叫。
→ 使用 trace-cmd 追蹤 ieee80211_scan_work.

- 問題：掉線頻繁
→ 檢查 cfg80211_disconnected 調用原因。
→ 比對 RSSI, power save, beacon miss log。

## 10. 學習建議

- 在開發板上使用 iw / wpa_cli 熟悉 nl80211 行為。

- 用 trace-cmd 觀察 cfg80211_scan → mac80211 → driver 流程。

- 寫一個 fake WiFi driver（只實作 .scan callback），學習與 cfg80211 互動。

- 分析真實驅動（如 ath9k, iwlwifi, rkwifi）的 probe 與註冊流程。

- 延伸閱讀

  - Documentation/networking/mac80211-injection.rst

  - Documentation/networking/nl80211.rst

  - Source: net/wireless/, drivers/net/wireless/
