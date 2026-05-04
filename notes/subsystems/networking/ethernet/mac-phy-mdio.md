
## 🧠 MAC / PHY / MDIO 架構與運作

本章節重點：

-   MAC / PHY 分工
-   MDIO bus 是什麼
-   phylib 如何管理 PHY
-   link up / negotiation flow
-   bring-up 與 debug 方法

----------

## 🧩 1. Ethernet 硬體分層

```
CPU
 ↓
MAC (Ethernet controller)
 ↓  (RGMII / RMII / SGMII)
PHY (physical layer chip)
 ↓
RJ45 / cable
```

----------

## 🔌 2. MAC vs PHY 分工

### 🧠 MAC（Media Access Controller）

👉 寫 driver 的地方（stmmac / fec）

負責：

```
TX/RX packet
DMA
descriptor ring
interrupt
```

----------

### 🧠 PHY（Physical Layer）

👉 外部晶片（Realtek / Marvell / TI）

負責：

```
電訊號轉換
link negotiation
speed / duplex
cable detection
```

----------

## 🔗 3. MAC 與 PHY 的連線（phy-mode）
```
phy-mode 決定 MAC 與 PHY 之間「如何傳輸資料」
```

### 常見模式：  
  
| 模式 | 說明 | 速度 | 使用場景 |  
|-------|-------------------------|----------|----------------------|  
| MII | 傳統 parallel bus | 100 Mbps | 舊設備 |  
| RMII | Reduced pins | 100 Mbps | MCU / low pin |  
| RGMII | Reduced Gigabit | 1 Gbps | 最常見（SoC） |  
| SGMII | Serial（SerDes） | 1 Gbps+ | 高速 / switch |

### 🧠 各模式直覺理解

### 🔹 MII

```
很多線（data + clock）簡單但腳位多
```

----------

### 🔹 RMII

```
減少腳位（2-bit data）需要 reference clock（50MHz）
```

👉 常見在：

-   MCU
-   低成本平台

----------

### 🔹 RGMII

```
4-bit data（DDR）125MHz clock
```

👉 特點：

```
少腳位 + 支援 1Gbps
```

👉 ⚠️ 但有一個超重要問題：

```
clock 與 data 需要 delay（skew）
```
----------

### 🔹 SGMII

```
高速 serial（類似 PCIe）
```

👉 常見：

-   switch
-   high-speed PHY
-   DSA CPU port
----------

DTS：

```
phy-mode = "rgmii";
```

👉 如果設錯：

```
link up 但不能傳或 完全沒有 link
```

----------

## 🧠 4. MDIO 是什麼？

```
MAC
 ↓
MDIO bus
 ↓
PHY register
```

👉 用來：

```
讀寫 PHY register
控制 link / speed / status
```

----------

### 📌 MDIO 類似：

```
"Ethernet 專用的 I2C"
```

----------

## 🧩 5. PHY Register（Clause 22）
  
| Register | 功能 |  
|----------|---------------------|  
| 0 | BMCR（control） |  
| 1 | BMSR（status） |  
| 4 | Advertisement |  
| 5 | Link partner |
----------

### 🔍 範例：讀 PHY

```
mdio-tool read eth0 1 0
```

或：

```
ethtool eth0
```

----------

## 🧠 6. phylib（Linux PHY framework）

```
MAC driver
   ↓
phylib
   ↓
PHY driver
```

----------

### driver 通常做：

```
phy_connect(dev, phy_name, handler, flags, interface);
```

或：

```
of_phy_connect()
```

----------

### phylib 負責：

```
auto negotiation
link state machine
speed/duplex update
callback driver
```

----------

## 🔁 7. Link up flow

```
1. PHY reset
2. PHY auto-negotiation
3. link partner 回應
4. speed / duplex 決定
5. PHY link up
6. 通知 MAC driver
7. MAC enable TX/RX
```

----------

### 📌 Kernel log

```
dmesg | grep eth
```

常見：

```
eth0: Link is Up - 1000Mbps/Full
```

----------

## 🔄 8. PHY state machine

```
DOWN
 ↓
STARTING
 ↓
AN（auto-negotiation）
 ↓
RUNNING
```

----------

### kernel 內部：

```
phy_state_machine()
```

----------

## 🔧 9. DTS 描述

```
ethernet@... {
    phy-mode = "rgmii";

    phy-handle = <&phy0>;
};

mdio {
    phy0: ethernet-phy@1 {
        reg = <1>;
    };
};
```

----------

### 📌 重點

```
reg = <1>  → PHY address
```

----------

## 🧪 10. Bring-up Flow

```
1. probe MAC driver
2. init MDIO bus
3. scan PHY
4. attach PHY
5. start PHY
6. link up
```

----------

## 🔍 11. Debug Playbook

### ✔ PHY 有沒有抓到？

```
dmesg | grep phy
```

----------

### ✔ MDIO 有沒有動？

```
dmesg | grep mdio
```

----------

### ✔ link 狀態

```
ethtool eth0
```

----------

### ✔ PHY register

```
mdio-tool dump eth0 1
```

----------

### ✔ driver

```
ethtool -i eth0
```

----------

## ❗ 12. 常見錯誤


### ❌ 沒有 PHY

```
No PHY found
```

👉 檢查：

-   DTS phy-handle
-   MDIO bus
-   PHY address

----------

### ❌ link down

👉 檢查：

```
線
switch
PHY power/reset
```

----------

### ❌ link up 但不通

👉 90% 是：

```
phy-mode 錯
RGMII delay 沒設
```

----------

### ❌ intermittent link

👉 檢查：

```
clock
reset timing
power
```

----------

## 🧠 13. debug 觀點

debug 時要分清：

```
問題在 MAC？
還是 PHY？
還是 MDIO？
```

----------

### 判斷方式：

| 現象 | 問題層 |  
|------------------|---------------|  
| 無 interface | Driver |  
| PHY 抓不到 | MDIO |  
| link 不上 | PHY |  
| link 上但不通 | MAC / phy-mode|

