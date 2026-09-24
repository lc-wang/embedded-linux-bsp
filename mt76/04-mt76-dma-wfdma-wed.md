
# 第 4 章：DMA / WFDMA / WED 架構與 TX/RX 資料流

# 4.1 本章目標

真正的資料傳輸依賴以下核心機制：

-   **DMA rings**
    
-   **WFDMA (Wireless Front-end DMA)**
    
-   **WED (Wireless Ethernet Dispatcher)**
    
-   **TX/RX descriptor**
    
-   **queue scheduling**
    

本章會完整解析：
```
skb → mt76 → DMA → WFDMA → WiFi MAC → air  
air → WiFi MAC → WFDMA → DMA → mt76 → mac80211
```
----------

# 4.2 mt76 Data Plane 架構

mt76 的資料平面設計如下：
```
mac80211  
 │  
 │ skb  
 ▼  
mt76 core  
 │  
 │ queue scheduling  
 ▼  
WFDMA (DMA engine)  
 │  
 ▼  
WiFi MAC / PHY  
 │  
 ▼  
Air
```
反向 RX path：
```
Air  
 │  
 ▼  
WiFi MAC  
 │  
 ▼  
WFDMA  
 │  
 ▼  
RX DMA ring  
 │  
 ▼  
mt76 RX handler  
 │  
 ▼  
mac80211
```
----------

# 4.3 DMA Ring 設計

Wi-Fi driver 幾乎都採用 **ring buffer + descriptor** 架構。

mt76 也不例外。

基本概念：
```
+----------------------------------+  
| descriptor | descriptor | ...    |  
+----------------------------------+  
 ▲  
 │ head (driver)  
 │  
 ▼  
 tail (hardware)
```
兩個重要 pointer：

| Pointer | 說明 |  
|--------|------|  
| head | Driver 新增 descriptor |  
| tail | Hardware 已處理 descriptor |

----------

# 4.4 mt76 queue abstraction

mt76 使用以下資料結構抽象 queue：
```
struct  mt76_queue {  
  void  *desc;  
  dma_addr_t  desc_dma;  
  
  u16  head;  
  u16  tail;  
  
  u16  ndesc;  
};
```
重要欄位：

| 欄位 | 說明 |  
|----------|--------------------------|  
| desc | Descriptor array |  
| desc_dma | DMA address |  
| head | Driver write pointer |  
| tail | Device read pointer |  
| ndesc | Queue size |
----------

# 4.5 TX 資料流

TX path 起點是：
```
mac80211 → ieee80211_ops.tx()
```
對應 mt76：
```
mt76_mac80211_tx()
```
流程如下：
```
mac80211  
 │  
 ▼  
mt76_mac80211_tx  
 │  
 ▼  
mt76_tx  
 │  
 ▼  
mt76_dma_tx_queue_skb  
 │  
 ▼  
WFDMA TX ring  
 │  
 ▼  
Hardware transmission
```
----------

# 4.6 TX Descriptor（TXWI）

MediaTek TX descriptor 叫做：

TXWI (Transmit Wireless Info)

典型格式：
```
struct  mt76_txwi {  
  __le32  txd1;  
  __le32  txd2;  
  __le32  txd3;  
  __le32  txd4;  
};
```
TXWI 包含：

| 欄位 | 說明 |  
|------|-------------------------|  
| WCID | Station ID |  
| rate | TX rate |  
| bw | Bandwidth |  
| ack | 是否需要 ACK |  
| power| TX power |  
| flags| Aggregation / retry |

----------

# 4.7 RX Descriptor（RXWI）

RX descriptor 叫：

RXWI

典型內容：
```
struct  mt76_rxwi {  
  __le32  rxd1;  
  __le32  rxd2;  
  __le32  rxd3;  
  __le32  rxd4;  
};
```
RXWI 包含：

| 欄位 | 說明 |  
|--------|-----------------|  
| RSSI | Signal strength |  
| rate | Receive rate |  
| band | 2G / 5G / 6G |  
| flags | AMPDU / AMSDU |  
| antenna| RX chain |

----------

# 4.8 WFDMA（Wireless Front-end DMA）

WFDMA 是 MediaTek Wi-Fi SoC 的 DMA engine。

功能：

-   TX descriptor fetch
    
-   RX descriptor write
    
-   interrupt trigger
    
-   queue scheduling
    

簡化架構：
```
Host memory  
 │  
 ▼  
DMA ring  
 │  
 ▼  
WFDMA engine  
 │  
 ▼  
WiFi MAC
```
WFDMA register 通常定義於：
```
drivers/net/wireless/mediatek/mt76/<chip>/regs.h
```
----------

# 4.9 RX path（資料接收）

RX path：
```
Air  
 │  
 ▼  
WiFi MAC  
 │  
 ▼  
WFDMA  
 │  
 ▼  
RX DMA ring  
 │  
 ▼  
mt76 RX handler  
 │  
 ▼  
mac80211
```
driver 主要做：

1.  檢查 RX descriptor
    
2.  建立 skb
    
3.  填寫 rx_status
    
4.  呼叫 mac80211
    

----------

# 4.10 WED（Wireless Ethernet Dispatcher）


WED 是 **MediaTek SoC networking accelerator hardware**。

它不是 Wi-Fi MAC 的一部分，而是 **SoC networking subsystem 的硬體加速模組**。

WED 主要用於：

-   降低 CPU packet processing 負擔
    
-   提升 Wi-Fi throughput
    
-   offload RX reorder

----------
## 4.10.1 WED 在 SoC 中的位置

MediaTek SoC networking pipeline：
```
 +-------------+  
 |    CPU      |  
 | Linux kernel|  
 +-------------+  
 │  
 ▼  
 +--------+  
 |  WED   |  
 +--------+  
 │  
 ▼  
 WFDMA  
 │  
 ▼  
 WiFi MAC
```
## 4.10.2 為什麼需要 WED

沒有 WED 時：
```
WiFi MAC  
 │  
 ▼  
WFDMA  
 │  
 ▼  
Host memory  
 │  
 ▼  
CPU interrupt  
 │  
 ▼  
NAPI poll  
 │  
 ▼  
mac80211
```
問題：

-   interrupt rate 高
    
-   RX reorder 在 CPU
    
-  CPU 成為 throughput bottleneck
    
----------

## 4.10.3 WED Offload 功能

WED 可 offload：

| 功能 | 說明 |  
|-------------------------|--------------------------|  
| RX interrupt moderation | 降低 interrupt rate |  
| RX buffer management | 管理 RX buffer |  
| Packet forwarding | DMA 直接轉送封包 |  
| RRO | Reorder offload |

其中 **RRO（Reorder Offload）** 是最重要功能之一。

----------

## 4.10.4 RRO（Reorder Offload）

802.11 AMPDU packet 常常 out-of-order：
```
expected: 1 2 3 4 5  
arrived : 1 3 2 5 4
```
沒有 RRO：
```
RX → CPU reorder → mac80211
```
有 RRO：
```
RX → WED reorder → mac80211
```
CPU 負擔大幅降低。

----------
## 4.10.5 Interrupt batching

WED 也會減少 interrupt。

沒有 WED：
```
packet → interrupt
```
有 WED：
```
packet1  
packet2  
packet3  
packet4  
 ↓  
single interrupt
```
這種 **batch completion** 可以降低 interrupt rate。

----------

## 4.10.6 WED 與 mt76 driver 的關係

WED driver 位於：
```
drivers/net/ethernet/mediatek/mtk_wed.c
```
mt76 driver 會：
```
register wed device  
setup wed rx rings  
enable wed offload
```
----------

# 4.11 WED2（Wi-Fi 7）

在 **MT7996 / MT7988** 上，WED 進化為 **WED2**。

新增能力：

| Feature | 說明 |  
|--------------------|--------------------------|  
| Multi RX rings | 多 RX DMA ring |  
| MLO support | Multi-Link Operation |  
| Higher throughput | 支援 Wi-Fi 7 traffic |  
| Improved RRO | 更大的 reorder window |
    

----------


# 4.12 不同晶片 DMA 架構

| Chipset | TX ring | RX ring | WED |  
|--------|----------|----------|------|  
| mt7915 | 4 | 2 | Yes |  
| mt7921 | 4 | 1 | No |  
| mt7986 | 4 | 2 | WED |  
| mt7996 | Multiple | Multiple | WED2 |

mt76 core 對這些差異做了抽象化。

----------
# 4.13 CPU bottleneck 為什麼仍存在

即使使用 DMA：

CPU 仍需處理：

-   interrupt
    
-   descriptor parsing
    
-   skb allocation
    
-   protocol processing
    

因此 throughput 常受：
```
packets/sec
```
而非
```
bandwidth
```
限制。

----------

# 4.14 為什麼 mt76 DMA 設計可以長期維護？

原因：

1. **queue abstraction**
```
struct mt76_queue
```
2. **bus abstraction**
```
mt76_bus_ops
```
3. **chipset ops**
```
mt76_driver_ops
```
這讓：

-   新 Wi-Fi generation（Wi-Fi 7）
    
-   新 SoC（Filogic）
    

都可以重用 mt76 core。
