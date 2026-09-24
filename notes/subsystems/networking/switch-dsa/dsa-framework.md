# Linux DSA Framework

本章節重點：

-   DSA 在 kernel 裡怎麼實作
-   `dsa_switch` / `dsa_port` 是什麼
-   CPU port / user port 在 kernel 的表示
-   封包如何在 CPU ↔ switch 間流動（tagging）

## 1. DSA 在 Linux 的位置

```text
User space
  ↓
ip / bridge / vlan
  ↓
net_device（lan1 / lan2）
  ↓
DSA core
  ↓
Switch driver
  ↓
Switch chip
```

## 2. 核心資料結構

### 2.1 `dsa_switch`

```c
struct dsa_switch {
    const struct dsa_switch_ops *ops;
    int num_ports;
    ...
};
```

代表：

```text
一顆 switch 晶片
```

### 2.2 `dsa_port`

```c
struct dsa_port {
    int index;
    struct net_device *netdev;
    ...
};
```

代表：

```text
switch 上的一個 port
```

### 2.3 port 類型

```text
CPU port
USER port
DSA port（cascade）
```

#### kernel 定義

```text
DSA_PORT_TYPE_CPU
DSA_PORT_TYPE_USER
```

## 3. net_device mapping

### 3.1 每個 user port → 一個 netdev

```text
ip link
```

```text
lan1
lan2
lan3
```

kernel：

```text
dsa_port → net_device
```

### 3.2 CPU port 不一定 exposed

```text
通常是 eth0
```

## 4. Packet Flow（核心）

### 4.1 TX（CPU → LAN）

```text
lan1 netdev
 ↓
DSA core
 ↓
add tag（port id）
 ↓
eth0（CPU port）
 ↓
MAC driver
 ↓
Switch chip
 ↓
forward 到 LAN1
```

### 4.2 RX（LAN → CPU）

```text
Wire
 ↓
PHY
 ↓
Switch
 ↓
加 tag（來源 port）
 ↓
CPU port
 ↓
MAC
 ↓
DSA core
 ↓
拆 tag
 ↓
lan1 netdev
```

### 4.3 重點

```text
CPU 與 switch 溝通 = 一定要 tag
```

## 5. tagging protocol

每個 switch vendor 都不同：

```text
Broadcom tag
Marvell tag
Realtek tag
```

### 5.1 kernel interface

```c
struct dsa_device_ops {
    int (*xmit)(...);
    ...
};
```

driver 負責：

```text
加 tag / 拆 tag
```

## 6. Switch driver model

### 6.1 driver 要實作

```c
struct dsa_switch_ops {
    int (*setup)(struct dsa_switch *ds);
    int (*port_enable)(...);
    int (*port_disable)(...);
    ...
};
```

### 6.2 flow

```text
probe
 ↓
register switch
 ↓
create ports
 ↓
create netdev
```

## 7. 與 PHY 的關係

```text
switch
 ├── port1 → PHY1
 ├── port2 → PHY2
 └── port3 → PHY3
```

kernel：

```text
每個 port 可能有 phydev
```

## 8. Bring-up Debug

### 8.1 DSA 是否啟動

```bash
dmesg | grep dsa
```

### 8.2 port 是否建立

```text
ip link
```

### 8.3 CPU port 正常嗎？

```bash
ethtool eth0
```

### 8.4 user port

```bash
ethtool lan1
```

## 9. 常見問題與排查

### 9.1 沒有 lan1~lan4

原因：

```text
DSA DTS 沒設好
switch driver 沒起來
```

### 9.2 lan1 link up 但不通

可能：

```text
CPU port timing（RGMII）
tagging 錯
```

### 9.3 封包收不到

檢查：

```text
tagging
DSA core
```

## 10. Debug 思維

```text
eth0 OK → CPU port OK
lan1 不通 → switch / tagging / PHY
```

## 11. Trace

### 11.1 trace TX

```bash
echo net_dev_xmit > /sys/kernel/debug/tracing/set_event
```

### 11.2 driver log

```c
pr_info("DSA xmit port=%d\n", port);
```

## 12. 總結

```text
DSA = 用 Linux netdev 抽象 switch 的每個 port
```
