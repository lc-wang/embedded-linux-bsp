
## 🧠 Switch Architecture

本章節重點：

-   為什麼需要 switch
-   單一 MAC vs 多 port 架構差異
-   CPU port / user port 是什麼
-   Linux / DSA 之前與之後的差異

----------

## 🧩 1. 單一 Ethernet

```
CPU
 ↓
MAC
 ↓
PHY
 ↓
RJ45
```

👉 一個 interface：

```
eth0
```

----------

## 🧩 2. Switch 架構

```
CPU
 ↓
MAC
 ↓
CPU port
 ↓
+------------------+
|   Switch chip    |
|                  |
|  port1 (LAN1)    |
|  port2 (LAN2)    |
|  port3 (LAN3)    |
|  port4 (LAN4)    |
+------------------+
```

----------

## 🔌 3. Port 分類

----------

### 🔹 CPU port

```
Switch ↔ CPU 的連接
```

👉 特性：

-   通常是 **RGMII / SGMII**
-   只有一條


### 🔹 User port

```
接 RJ45 / LAN port
```

👉 每個 port：

-   有自己的 PHY（或內建 PHY）
-   對應實體網路孔


## 🧠 4. 封包 flow


### 📤 TX（CPU → LAN）

```
CPU (eth0)
 ↓
MAC
 ↓
CPU port
 ↓
Switch
 ↓
選擇 port（LAN1）
 ↓
PHY
 ↓
Wire
```

----------

### 📥 RX（LAN → CPU）

```
Wire
 ↓
PHY
 ↓
Switch
 ↓
CPU port
 ↓
MAC
 ↓
CPU
```

----------

## ⚠️ 重點

```
CPU 只看到一個 MAC（eth0）但實際有多個 port
```

----------

# 🧠 5. 沒有 DSA 的世界

## ❌ 傳統做法（vendor driver）

```
eth0 → 整個 switch
```

👉 問題：

-   看不到個別 port
-   無法用標準工具（bridge / vlan）

----------

👉 常見：

```
ethsw / ethss driver
```

----------

# 🚀 6. 有 DSA 的世界

👉 Distributed Switch Architecture

----------

## ✔ 每個 port 都變 interface

```
ip link
```

看到：

```
eth0        (CPU port)
lan1
lan2
lan3
lan4
```

----------

## ✔ 可以用標準 Linux 工具

```
bridge
vlan
tc
```

----------

# 🔗 7. DSA 核心概念


## 🔹 tagging

```
CPU ↔ Switch 的封包需要 tag
```

👉 用來：

```
告訴 switch 要去哪個 port
```


## 🔹 switch forwarding

```
switch 內部會自己轉封包
```

👉 CPU 不一定會看到所有流量


## 🔹 offloading

```
switch 幫你做 forwarding
```

👉 CPU 不需要處理

----------

# 🧪 8. Bring-up 重點

----------

## ✔ CPU port 正常嗎？

👉 這就是：

```
eth0 要先正常
```

----------

## ✔ switch driver 有沒有起來？

```
dmesg | grep dsa
```

----------

## ✔ port 有沒有出現？

```
ip link
```

----------

## ✔ link 狀態

```
ethtool lan1
```

----------

# ❗ 9. 常見問題


## ❌ 只有 eth0 沒有 lan1~lan4

👉 原因：

```
DSA 沒啟動或 DTS 沒設
```

## ❌ lan port link up 但不通

👉 可能：

```
CPU port timing（RGMII）
```

## ❌ switch 完全沒反應

👉 檢查：

```
SPI / MDIO / reset
```

----------

# 🧠 10. 理解

要想成：

```
DSA = Ethernet + 多 PHY + switch forwarding
```

