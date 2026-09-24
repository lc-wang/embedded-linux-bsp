# DSA Debug Playbook

本章節目標：

-   定位 DSA 問題
-   分清 CPU port / tagging / VLAN / PHY
-   提供可直接用的 debug SOP

## 1. DSA Debug

```
DSA 問題 = 分 4 層看

1. CPU port（eth0）
2. Switch（DSA driver）
3. Port（lanX）
4. VLAN / bridge
```

一定要「分層」，不要亂試

## 2. 定位流程

### 2.1 Step 1：有沒有 lanX？

```
ip link
```

#### 沒有 lan1 / lan2

問題在：

```
DSA 沒起來
DTS 錯
driver 沒 register
```

### 2.2 Step 2：CPU port 正常嗎？

```
ethtool eth0
```

#### link down

問題在：

```
MAC / PHY / RGMII（CPU port）
```

#### link up

進下一步

### 2.3 Step 3：lanX link 狀態

```
ethtool lan1
```

#### link down

問題在：

```
PHY / 線 / switch port
```

#### link up

進下一步

### 2.4 Step 4：有沒有封包？

```
tcpdump -i lan1
```

#### 沒封包

問題在：

```
tagging / forwarding / VLAN
```

### 2.5 Step 5：bridge / VLAN

```
bridge link
bridge vlan show
```

## 3. Debug Decision Tree

```
沒有 lanX?
  → DSA / DTS

lanX 有，但 link down?
  → PHY / port

link up，但不通?
  → CPU port / RGMII / tagging

bridge 不通?
  → VLAN / offload
```

## 4. 指令 Sheet

### 4.1 interface

```
ip link
```

### 4.2 CPU port

```
ethtool eth0
```

### 4.3 port

```
ethtool lan1
```

### 4.4 bridge

```
bridge link
bridge fdb show
```

### 4.5 VLAN

```
bridge vlan show
```

### 4.6 封包

```
tcpdump -i lan1
tcpdump -i eth0
```

### 4.7 DSA log

```
dmesg | grep dsa
```

## 5. 錯誤（DSA）

### 5.1 Case 1：eth0 OK，但 lanX 全壞

90%：

```
CPU port RGMII delay 錯
```

### 5.2 Case 2：lan1 link up 但 ping 不通

可能：

```
tagging 錯
CPU port timing
```

### 5.3 Case 3：lan1 ↔ lan2 不通

檢查：

```
bridge link
```

原因：

```
沒有 bridge
```

### 5.4 Case 4：bridge 有設但不通

檢查：

```
bridge vlan show
```

原因：

```
VLAN mismatch
```

### 5.5 Case 5：CPU 收到封包，但 lanX 沒有

原因：

```
tag parsing 錯
DSA driver bug
```

### 5.6 Case 6：intermittent

通常：

```
clock / reset / RGMII skew
```

## 6. 進階 Debug

### 6.1 看 CPU port 流量

```
tcpdump -i eth0
```

如果看到：

```
有 packet，但 lanX 沒有
```

問題在：

```
DSA tagging / demux
```

### 6.2 看 lanX 流量

```
tcpdump -i lan1
```

### 6.3 FDB

```
bridge fdb show
```

看：

```
MAC → port mapping
```

### 6.4 強制 speed

```
ethtool -s eth0 speed 1000 duplex full autoneg off
```

排除 negotiation 問題

## 7. Debug 觀念

### 7.1 Rule 1

```
eth0 = CPU port（唯一出口）
```

### 7.2 Rule 2

```
lanX 不是真正送封包
```

### 7.3 Rule 3

```
tagging 錯 = 全部壞
```

### 7.4 Rule 4

```
CPU port timing 錯 = 全滅
```

### 7.5 Rule 5

```
bridge / VLAN 錯 = 封包消失
```

## 8. Debug 範例

### 8.1 Case：link up 但完全不通

debug：

```
ethtool eth0
ethtool lan1
```

發現：

```
全部 link up
```

下一步：

```
tcpdump -i eth0
```

有 packet

再看：

```
tcpdump -i lan1
```

沒有

結論：

```
DSA tagging / CPU port timing 問題
```
