## 🧠 Linux DSA Framework

本章節重點：

-   DSA 在 kernel 裡怎麼實作
-   `dsa_switch` / `dsa_port` 是什麼
-   CPU port / user port 在 kernel 的表示
-   封包如何在 CPU ↔ switch 間流動（tagging）

----------

## 🧩 1. DSA 在 Linux 的位置

```
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


## 🔗 2. 核心資料結構

## 🔹 `dsa_switch`

```
struct dsa_switch {
    const struct dsa_switch_ops *ops;
    int num_ports;
    ...
};
```

👉 代表：

```
一顆 switch 晶片
```

## 🔹 `dsa_port`

```
struct dsa_port {
    int index;
    struct net_device *netdev;
    ...
};
```

👉 代表：

```
switch 上的一個 port
```

## 🔹 port 類型

```
CPU port
USER port
DSA port（cascade）
```

----------

### 📌 kernel 定義

```
DSA_PORT_TYPE_CPU
DSA_PORT_TYPE_USER
```

----------

# 🔌 3. net_device mapping

## ✔ 每個 user port → 一個 netdev

```
ip link
```

```
lan1
lan2
lan3
```

----------

👉 kernel：

```
dsa_port → net_device
```

----------

## ✔ CPU port 不一定 exposed

```
通常是 eth0
```

----------

# 🔁 4. Packet Flow（核心）

## 📤 TX（CPU → LAN）

```
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

----------

## 📥 RX（LAN → CPU）

```
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

## ⚠️ 重點

```
CPU 與 switch 溝通 = 一定要 tag
```

----------

# 🧩 5. tagging protocol

👉 每個 switch vendor 都不同：

```
Broadcom tag
Marvell tag
Realtek tag
```

----------

## kernel interface

```
struct dsa_device_ops {
    int (*xmit)(...);
    ...
};
```

----------

👉 driver 負責：

```
加 tag / 拆 tag
```

# 🔧 6. Switch driver model

## driver 要實作：

```
struct dsa_switch_ops {
    int (*setup)(struct dsa_switch *ds);
    int (*port_enable)(...);
    int (*port_disable)(...);
    ...
};
```

----------

## flow

```
probe
 ↓
register switch
 ↓
create ports
 ↓
create netdev
```

----------

# 🔗 7. 與 PHY 的關係

```
switch
 ├── port1 → PHY1
 ├── port2 → PHY2
 └── port3 → PHY3
```

----------

👉 kernel：

```
每個 port 可能有 phydev
```


# 🧪 8. Bring-up Debug

## ✔ DSA 是否啟動

```
dmesg | grep dsa
```

----------

## ✔ port 是否建立

```
ip link
```

----------

## ✔ CPU port 正常嗎？

```
ethtool eth0
```

----------

## ✔ user port

```
ethtool lan1
```

----------

# ❗ 9. 常見錯誤


## ❌ 沒有 lan1~lan4

👉 原因：

```
DSA DTS 沒設好
switch driver 沒起來
```


## ❌ lan1 link up 但不通

👉 可能：

```
CPU port timing（RGMII）
tagging 錯
```

## ❌ 封包收不到

👉 檢查：

```
tagging
DSA core
```

----------

# 🧠 10. Debug 思維

```
eth0 OK → CPU port OK
lan1 不通 → switch / tagging / PHY
```

# 🔍 11. Trace

## ✔ trace TX

```
echo net_dev_xmit > /sys/kernel/debug/tracing/set_event
```

----------

## ✔ driver log

```
pr_info("DSA xmit port=%d\n", port);
```

----------

# 🧠 12. 總結

```
DSA = 用 Linux netdev 抽象 switch 的每個 port
```
