# Ethernet Debug Playbook

本章節目標：

-   快速定位問題層級
-   提供可直接複製的 debug 指令
-   整理常見錯誤 pattern

## 1. Debug 思維

```
Ethernet 問題永遠分層看：Driver → MDIO → PHY → Link → Packet
```

## 2. 快速定位流程

### 2.1 Step 1：有沒有 interface？

```
ip link
```

#### 沒有 eth0

問題在：

```
Driver / probe / DTS
```

### 2.2 Step 2：driver 是誰？

```
ethtool -i eth0
```

確認：

-   stmmac / fec / eqos

### 2.3 Step 3：link 狀態

```
ethtool eth0
```

#### Link detected: no

看 PHY / 線 / switch

#### Link detected: yes

進下一步

### 2.4 Step 4：有沒有封包？

```
ping 8.8.8.8
tcpdump -i eth0
```

#### 沒封包

問題在：

```
MAC / RGMII / DMA
```

## 3. 指令 Cheat Sheet

### 3.1 基本狀態

```
ip link
ip addr
```

### 3.2 driver

```
ethtool -i eth0
```

### 3.3 link

```
ethtool eth0
```

### 3.4 statistics

```
cat /proc/net/dev
ethtool -S eth0
```

### 3.5 PHY（MDIO）

```
mdio-tool dump eth0 1
```

### 3.6 封包

```
tcpdump -i eth0
```

### 3.7 carrier

```
cat /sys/class/net/eth0/carrier
```

## 4. 問題整理

### 4.1 Case 1：沒有 eth0

#### 現象

```
ip link 看不到 eth0
```

#### 原因

-   driver 沒 probe
-   DTS 錯

#### Debug

```
dmesg | grep eth
```

### 4.2 Case 2：No PHY found

#### 現象

```
dmesg:No PHY found
```

#### 原因

-   PHY address 錯
-   MDIO bus 問題

#### Debug

```
dmesg | grep mdio
```

### 4.3 Case 3：link down

#### 現象

```
Link detected: no
```

#### 原因

-   線
-   switch
-   PHY reset / power

### 4.4 Case 4：link up 但 ping 不通

#### 現象

```
Link detected: yes
但 ping timeout
```

#### 90% 原因

```
RGMII delay 錯（phy-mode）
```

#### Debug

```
ethtool -S eth0
```

看：

```
rx_crc_errors
rx_errors
```

### 4.5 Case 5：TX OK，RX 沒有

#### 現象

-   ping 發出
-   收不到

#### 原因

```
IRQ / NAPI / DMA 問題
```

### 4.6 Case 6：RX OK，TX 卡住

#### 原因

```
queue stop
ndo_start_xmit 沒跑
```

### 4.7 Case 7：intermittent

#### 現象

-   有時 OK
-   有時斷

#### 原因

```
clock / reset / RGMII skew
```

## 5. 進階 Debug

### 5.1 強制 speed（排除 negotiation）

```
ethtool -s eth0 speed 100 duplex full autoneg off
```

### 5.2 檢查 queue

```
tc qdisc show dev eth0
```

### 5.3 ftrace

```
echo net_dev_xmit > /sys/kernel/debug/tracing/set_event
```

### 5.4 interrupt

```
cat /proc/interrupts
```

## 6. Debug Decision Tree

```
沒 eth0?
  → driver

有 eth0?
  → link up?
       → no → PHY / MDIO
       → yes → 有 packet?
              → no → MAC / RGMII
              → yes → OK
```
