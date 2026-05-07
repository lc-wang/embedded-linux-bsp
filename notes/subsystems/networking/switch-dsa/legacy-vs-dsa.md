
## 🧠 Legacy (ethss / ethsw) vs DSA（

本章節重點：

-   什麼是 `ethss / ethsw`
-   與 DSA 的核心差異
-   packet flow 對照
-   debug / DTS / driver 差異

----------

# 🧩 1. 兩個世界

## 🔴 Legacy（ethss / ethsw）

```
Vendor BSP（私有實作）
```

👉 常見：

-   NXP（FEC + switch）
-   Renesas（ethss）
-   TI CPSW（某些版本）
-   Realtek SDK

----------

## 🟢 DSA（Distributed Switch Architecture）

👉 Distributed Switch Architecture

```
Linux upstream 標準架構
```

----------

# 🔥 2. 架構對比

## 🔴 Legacy（ethss / ethsw）

```
CPU
 ↓
eth0（整個 switch）
 ↓
Switch chip
 ├── port1
 ├── port2
 └── port3
```

👉 特點：

```
只有一個 netdev（eth0）
```

----------

## 🟢 DSA

```
CPU
 ↓
eth0（CPU port）
 ↓
Switch
 ├── lan1
 ├── lan2
 └── lan3
```

👉 特點：

```
每個 port 都有 netdev
```

----------

# 🔁 3. Packet Flow 對照


## 🔴 Legacy（ethss）

### TX

```
CPU
 ↓
eth0
 ↓
Switch
 ↓
portX（由 driver 決定）
```

----------

### RX

```
Switch
 ↓
eth0
 ↓
CPU
```

👉 CPU **不知道來源 port**


## 🟢 DSA

### TX

```
lan1
 ↓
DSA core
 ↓
tagging（port id）
 ↓
eth0
 ↓
Switch
 ↓
port1
```

----------

### RX

```
Switch
 ↓
tagged packet
 ↓
eth0
 ↓
DSA core
 ↓
拆 tag
 ↓
lan1
```

----------

# ⚠️ 4. 核心差異

## 🔹 是否有「port awareness」

| 架構 | CPU 是否知道 port |  
|--------|-------------------|  
| Legacy | ❌ 不知道 |  
| DSA | ✅ 知道 |


## 🔹 是否使用 tagging

| 架構 | tagging |  
|--------|---------|  
| Legacy | ❌ |  
| DSA | ✅ |


## 🔹 net_device 模型

| 架構 | netdev |  
|--------|------------------|  
| Legacy | eth0 |  
| DSA | eth0 + lanX |

## 🔹 forwarding

| 架構 | forwarding |  
|--------|-------------------|  
| Legacy | driver 控制 |  
| DSA | switch offload |


# 🔧 5. Driver 架構差異

## 🔴 Legacy（ethsw / ethss）

```
eth driver（整合）
 ├── MAC
 ├── switch control
 ├── VLAN
 └── forwarding
```

👉 特點：

```
所有邏輯在 driver 裡
```

----------

## 🟢 DSA

```
MAC driver
DSA core
switch driver
```

👉 分層：

```
MAC（eth0）
Switch（DSA）
Port（lanX）
```

----------

# 🧩 6. Device Tree 差異

## 🔴 Legacy

```
ethernet@... {
    ...
    switch-config = ...;
};
```

👉 通常：

```
沒有標準格式（vendor-specific）
```

## 🟢 DSA

```
switch@0 {
    ports {
        port@0 { label = "cpu"; };
        port@1 { label = "lan1"; };
    };
};
```

👉 特點：

```
標準化 binding
```
# 🔍 7. Debug

## 🔴 Legacy

### 看到：

```
ip link
```

```
eth0
```

----------

### 問題：

```
不知道哪個 port 壞
```

----------

### Debug：

```
vendor tool / ioctl
```


## 🟢 DSA

### 看到：

```
ip link
```

```
eth0
lan1
lan2
```

### Debug：

```
ethtool lan1
tcpdump -i lan1
bridge vlan show
```

----------

# 🔥 8. 問題

## ❗ link up 但不通

| 架構 | 原因 |  
|--------|----------------------|  
| Legacy | switch config |  
| DSA | RGMII / tagging |


## ❗ VLAN 問題


| 架構 | 設定方式 |  
|--------|--------------|  
| Legacy | driver API |  
| DSA | bridge / vlan|


## ❗ port 不通

| 架構 | debug |  
|--------|------------|  
| Legacy | 看 driver |  
| DSA | 看 lanX |

----------

# 🚀 9. Migration（Legacy → DSA）


## 要做的轉換


### 🔹 netdev

```
eth0 → lanX
```

----------

### 🔹 config

```
vendor API → bridge / vlan
```

----------

### 🔹 debug

```
ioctl → ethtool / tcpdump
```
