# Bridge / VLAN Flow

本章節重點：

-   Linux bridge 在 DSA 上怎麼運作
-   VLAN tagging / untagging
-   switch 如何 offload forwarding（CPU 不參與）
-   真實封包 flow

## 1. 為什麼需要 bridge？

### 1.1 沒有 bridge

```text
lan1 ↔ lan2 無法互通
```

因為：

```text
每個 lanX 是獨立 net_device
```

### 1.2 使用 bridge

```text
ip link add br0 type bridge
ip link set lan1 master br0
ip link set lan2 master br0
```

變成：

```text
lan1 ↔ lan2 可以互通
```

## 2. Bridge 在 DSA 上的特殊性

### 2.1 一般 bridge（沒有 DSA）

```text
lan1 → CPU → lan2
```

CPU 負責 forwarding

### 2.2 DSA bridge（重點）

```text
lan1 → switch → lan2
```

CPU 不參與！

## 3. Hardware offload

```text
switch 自己轉封包
```

### 3.1 好處

```text
效能高CPU 負載低
```

### 3.2 Kernel 角色

```text
設定 switch forwarding table（FDB）
```

## 4. Flow 比較

### 4.1 Software forwarding

```text
lan1 → CPU → lan2
```

### 4.2 DSA offload

```text
lan1 → switch → lan2
```

CPU 完全看不到封包

## 5. FDB（Forwarding Database）

### 5.1 功能

```text
MAC address → port mapping
```

### 5.2 查看

```text
bridge fdb show
```

範例：

```text
00:11:22:33:44:55 dev lan1
```

## 6. VLAN 基本概念

### 6.1 VLAN = Virtual LAN

```text
把一個 switch 分成多個邏輯網路
```

### 6.2 Tagging（802.1Q）

IEEE 802.1Q

```text
封包加 VLAN ID
```

## 7. VLAN 在 DSA 上

### 7.1 設定 VLAN

```text
bridge vlan add dev lan1 vid 10
bridge vlan add dev lan2 vid 10
```

### 7.2 Flow

```text
lan1 (VID 10)
 ↓
switch
 ↓
只轉到 VID 10 的 port
```

## 8. VLAN tagging flow

### 8.1 TX

```text
lan1
 ↓
DSA
 ↓
加 VLAN tag
 ↓
switch
 ↓
forward
```

### 8.2 RX

```text
switch
 ↓
tagged packet
 ↓
DSA
 ↓
拆 tag
 ↓
lan1
```

## 9. CPU port 與 VLAN

```text
CPU port 也會 carry VLAN
```

如果設錯：

```text
封包會消失
```

## 10. 常見問題與排查

### 10.1 Case 1：lan1 ↔ lan2 不通

檢查：

```text
bridge link
```

### 10.2 Case 2：bridge 建了但沒 offload

現象：

```text
CPU usage 高
```

檢查：

```text
bridge link
```

看：

```text
offload
```

### 10.3 Case 3：VLAN 不通

檢查：

```text
bridge vlan show
```

### 10.4 Case 4：只有 CPU 收到封包

原因：

```text
switch 沒 forwarding（offload 沒開）
```

### 10.5 Case 5：封包消失

通常：

```text
VLAN mismatch
tagging 錯
```

## 11. Debug

### 11.1 bridge

```text
bridge link
bridge fdb show
```

### 11.2 VLAN

```text
bridge vlan show
```

### 11.3 封包

```bash
tcpdump -i lan1
```

## 12. Debug Flow

```text
不通？→ bridge 有設？→ VLAN 有對？→ offload 有沒有？
```

## 13. 觀念

### 13.1 Rule 1

```text
DSA + bridge = switch forwarding
```

### 13.2 Rule 2

```text
CPU 不一定看到封包
```

### 13.3 Rule 3

```text
VLAN 錯 → 封包直接消失
```
