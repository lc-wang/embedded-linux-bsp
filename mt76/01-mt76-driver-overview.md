# 第 1 章：mt76 家族與驅動定位

## 1. mt76 是什麼？

mt76 是 MediaTek 開源 Wi-Fi 的 SoftMAC 驅動，主線 Linux **唯一官方支援的 MTK mac80211 Wi-Fi 驅動**。

屬性：
-   **SoftMAC**（所有 MLME 都由 mac80211 控制）
-   **非 FullMAC**（不像 ath10k、iwlwifi）
-   **使用 Unified MCU Command 與韌體溝通**

## 2. 支援晶片家族

以下整理 mt76 驅動目前支援的晶片家族與其對應規格：

| 家族              | Wi-Fi 代數             | 介面             | 代表晶片 / 平台            |
|------------------|------------------------|------------------|----------------------------|
| **mt76x2**        | 802.11ac               | PCIe             | MT7612                     |
| **mt7603**        | 802.11n                | PCIe             | MT7603                     |
| **mt7615**        | 802.11ac Wave2         | PCIe             | MT7615                     |
| **mt7915 / mt7916** | 802.11ax (Wi-Fi 6)     | PCIe             | Filogic 830                |
| **mt7921 (E/U/SDIO)** | 802.11ax               | PCIe / USB / SDIO | MT7921K                    |
| **mt7996**        | 802.11be (Wi-Fi 7)     | PCIe             | Filogic 880                |
| **mt7981 / mt7986** | SoC 內建 AX             | SoC (內建 MAC/PHY) | OpenWrt 熱門平台            |

## 3. SoftMAC 架構定位

SoftMAC 驅動的責任清楚：

使用者 → nl80211 → cfg80211 → mac80211 → 驅動 → MCU → Wi-Fi HW

mt76 位於最底層：
-   處理 DMA/TX/RX
-   MCU 指令
-   EEPROM/EFUSE
-   PHY/RF 參數

mac80211 則負責：
-   STA 建立/刪除
-   AP beacon 設定
-   Channel switch
-   AMPDU/AMSDU

## 附錄

### A. 參考來源

1.  mt76 Driver — Linux Wireless Documentation: “mediatek — mac80211 wireless driver for MediaTek MT7xxx series…”。[Linux Wireless Documentation](https://wireless.docs.kernel.org/en/latest/en/users/drivers/mediatek.html?utm_source=chatgpt.com)
2.  mt76 Repository — OpenWrt GitHub: “mac80211 driver for MediaTek MT76x0e, MT76x2e, MT7615, …” [GitHub](https://github.com/openwrt/mt76?utm_source=chatgpt.com)
3.  Kernel config info: `CONFIG_MT76_CORE` on kernelconfig.io — shows inclusion since Linux 3.10 and file path. [KernelConfig](https://www.kernelconfig.io/config_mt76_core?utm_source=chatgpt.com)
4.  Phoronix article: “Many MediaTek Wireless Driver Improvements On Deck For Linux 5.8” — 提及 mt76 新裝置支援、開發動態。 [Phoronix](https://www.phoronix.com/news/MediaTek-Wireless-Linux-5.8?utm_source=chatgpt.com)
5.  Reddit 討論：使用者 “mt76 is the modern driver for most Mediatek chips … kernel part of the drivers is FOSS.” [Reddit](https://www.reddit.com/r/openwrt/comments/171pw7d/understanding_open_source_wifi_drivers_and_other/?utm_source=chatgpt.com)
