## 🧠 Linux Network Stack Overview

本章節從 **BSP / Driver 工程師角度**，說明 Linux 網路 stack 的整體架構，重點放在：

-   Ethernet driver 在整個 stack 的位置
-   封包從 userspace 到硬體的 flow
-   Debug / bring-up 時應該關注的層級

----------

## 🧩 1. 整體架構（由上到下）

+-----------------------------+  
|        User Space           |  
|  (ping / ip / socket API)   |  
+-----------------------------+  
 │  
 ▼  
+-----------------------------+  
|        Socket Layer         |  
+-----------------------------+  
 │  
 ▼  
+-----------------------------+  
|     Protocol Stack          |  
|  (TCP / UDP / IP / ARP)     |  
+-----------------------------+  
 │  
 ▼  
+-----------------------------+  
|       net_device API        |  
+-----------------------------+  
 │  
 ▼  
+-----------------------------+  
|   Ethernet Driver (MAC)     |  
|  (stmmac / fec / eqos)      |  
+-----------------------------+  
 │  
 ▼  
+-----------------------------+  
|        PHY Layer            |  
|   (MDIO / phylib / PHY)     |  
+-----------------------------+  
 │  
 ▼  
+-----------------------------+  
|         Hardware            |  
|     (MAC + PHY + RJ45)      |  
+-----------------------------+

----------

## 🔁 2. TX Flow（送封包）
```
User space  
 ↓ send()  
Socket layer  
 ↓  
TCP/IP stack  
 ↓  
dev_queue_xmit()  
 ↓  
Ethernet driver (ndo_start_xmit)  
 ↓  
DMA → MAC  
 ↓  
PHY → Wire
```
### 📌 關鍵點

-   driver entry point：
```
ndo_start_xmit()
```
-   常見 debug：
```
ethtool -S eth0  
cat /proc/net/dev
```
----------

## 🔁 3. RX Flow（收封包）
```
Wire  
 ↓  
PHY  
 ↓  
MAC (DMA)  
 ↓  
Interrupt / NAPI  
 ↓  
napi_poll()  
 ↓  
netif_receive_skb()  
 ↓  
TCP/IP stack  
 ↓  
socket buffer  
 ↓  
User space
```
### 📌 關鍵點

-   RX 通常走 **NAPI（polling）**
-   skb（socket buffer）是核心資料結構

----------

## 📦 4. 核心資料結構：`sk_buff`
```
struct  sk_buff {  
  unsigned  char  *data;  
  unsigned  int  len;  
 ...  
};
```
用途：

-   封裝一個 network packet
-   在整個 network stack 中傳遞

----------

## 🔌 5. `net_device`（Driver 核心）
```
struct  net_device {  
  const  struct  net_device_ops  *netdev_ops;  
 ...  
};
```
driver 會註冊：
```
dev->netdev_ops  =  &ops;
```
常見 ops：
```
.ndo_open  
.ndo_stop  
.ndo_start_xmit
```
----------

## 🔗 6. Ethernet Driver 在哪裡？

常接觸的 driver：

-   stmmac（Synopsys GMAC）
-   fec（NXP i.MX）
-   eqos（DesignWare EQOS）

這些 driver 負責：
```
DMA descriptor  
TX/RX ring buffer  
interrupt  
MAC register control
```
----------

## 🔬 7. PHY / MDIO 在 stack 中的位置
```
MAC driver  
 ↓  
phylib  
 ↓  
MDIO bus  
 ↓  
PHY chip
```
PHY 負責：

-   link up/down
-   speed (10/100/1000)
-   duplex

----------

## 🧪 8. Bring-up 觀察點

### 開機 log
```
dmesg | grep eth
```
### link 狀態
```
ip link  
ethtool eth0
```
### PHY
```
dmesg | grep phy
```
----------

## 🧰 9. 關注點

### ✔ Driver 層

-   DMA 是否正常
-   descriptor 是否跑
-   interrupt 是否進來

### ✔ PHY 層

-   MDIO 是否能讀
-   link 是否 up

### ✔ DTS

-   phy-mode 是否正確
-   clock / reset

