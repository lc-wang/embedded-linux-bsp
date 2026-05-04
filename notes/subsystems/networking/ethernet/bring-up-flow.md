
## 🧠 Ethernet Bring-up Flow

本章節重點：

-   從開機到 ping 成功的完整流程
-   每個階段對應 kernel 行為
-   每一步的檢查指令（debug checklist）

----------

## 🧭 1. 整體時間軸

```
Boot
 ↓
MAC driver probe
 ↓
MDIO init
 ↓
PHY attach
 ↓
register_netdev()
 ↓
----------------------
user: ip link up
----------------------
 ↓
PHY start + auto-negotiation
 ↓
Link Up
 ↓
TX/RX ready
 ↓
Ping OK
```

----------

# 🔍 2. Step-by-step


## 🧩 Step 1：Driver probe

```
kernel boot
 ↓
platform_driver → probe()
```

### ✔ 應該發生

-   MAC register mapping
-   DMA init
-   MDIO bus init

----------

### 🔍 檢查

```
dmesg | grep -i eth
```

----------

### ❌ 常見問題

-   driver 沒 bind
-   clock / reset 沒開

----------

## 🧩 Step 2：MDIO bus 初始化

```
MAC driver
 ↓
mdiobus_register()
```

----------

### ✔ 應該發生

-   掃描 PHY address
-   建立 MDIO bus

----------

### 🔍 檢查

```
dmesg | grep -i mdio
```

----------

### ❌ 問題

```
MDIO timeout
No PHY found
```

👉 通常：

-   DTS 錯
-   clock 問題

----------

## 🧩 Step 3：PHY attach

```
phy_connect()
或
of_phy_connect()
```

----------

### ✔ 應該發生

-   找到 PHY
-   建立 phy_device

----------

### 🔍 檢查

```
dmesg | grep -i phy
```

----------

### ❌ 問題

-   phy-handle 錯
-   address 錯

----------

## 🧩 Step 4：register_netdev

```
register_netdev(dev);
```

----------

### ✔ 應該發生

```
ip link
```

看到：

```
eth0
```

----------

### ❌ 問題

-   沒有 eth0 → driver 問題

----------

## 🧩 Step 5：ip link up

```
ip link set eth0 up
```

----------

### Kernel flow

```
dev_open()
 ↓
ndo_open()
```

----------

### ✔ driver 應該做

-   enable IRQ
-   start DMA
-   start PHY

----------

### 🔍 檢查

```
dmesg
```

----------

## 🧩 Step 6：PHY auto-negotiation

```
PHY
 ↓
交換能力（speed / duplex）
 ↓
決定 link
```

----------

### 🔍 檢查

```
ethtool eth0
```

----------

### ✔ 正常

```
Link detected: yes
Speed: 1000Mb/s
```

----------

### ❌ 問題

| 現象 | 原因 |  
|--------------|-------------|  
| link down | 線 / PHY |  
| link up 不穩 | timing |

----------

## 🧩 Step 7：Link Up → MAC enable

```
PHY → callback
 ↓
MAC driver
 ↓
enable TX/RX
```

----------

### Kernel log

```
eth0: Link is Up - 1000Mbps/Full
```

----------

## 🧩 Step 8：封包測試


### ✔ 基本測試

```
ping 8.8.8.8
```

----------

### ✔ deeper debug

```
tcpdump -i eth0
```

----------

### ✔ statistics

```
ethtool -S eth0
```

----------

# 🧪 3. Debug Checklist

## 🚨 情境 1：沒有 eth0

👉 檢查：

```
dmesg | grep eth
```

----------

## 🚨 情境 2：No PHY found

👉 檢查：

-   DTS phy-handle
-   MDIO bus
-   PHY address

----------

## 🚨 情境 3：link down

👉 檢查：

```
ethtool eth0
```

----------

## 🚨 情境 4：link up 但不通

👉 90%：

```
RGMII delay 問題
```

----------

## 🚨 情境 5：RX 沒有封包

👉 檢查：

-   IRQ
-   NAPI
-   descriptor

----------

## 🚨 情境 6：TX 卡住

👉 檢查：

```
ndo_start_xmit 是否被呼叫
queue 是否 stop
```

----------

# 🔧 4. Debug 指令整理


## 基本

```
ip link
ethtool eth0
```

----------

## driver

```
ethtool -i eth0
```

----------

## statistics

```
cat /proc/net/dev
ethtool -S eth0
```

----------

## PHY

```
mdio-tool dump eth0 1
```

----------

## 封包

```
tcpdump -i eth0
```

----------

# 🧠 5. BSP Debug 思維

這個 flow：

```
Driver → MDIO → PHY → Link → Packet
```

----------

## 快速定位

| 現象 | 層 |  
|----------------|-----------|  
| 沒 interface | driver |  
| PHY 抓不到 | MDIO |  
| link 不上 | PHY |  
| link 上但不通 | RGMII |  
| 有 TX 無 RX | MAC |

----------

# 🔥 6. 實戰技巧


## ✔ 強制 speed

```
ethtool -s eth0 speed 100 duplex full autoneg off
```

👉 排除 negotiation 問題

----------

## ✔ 查看 carrier

```
cat /sys/class/net/eth0/carrier
```

----------

## ✔ 查看 state

```
cat /sys/class/net/eth0/operstate
```


