
## 🧠 Switch Driver Model

本章節重點：

-   switch driver 的 probe flow
-   `dsa_register_switch()` 做了什麼
-   driver 要實作哪些 ops
-   與 MDIO / SPI / I2C 的關係

----------

# 🧩 1. Driver 在整體架構的位置

```
Platform / SPI / MDIO driver        ↓Switch driver（DSA）        ↓DSA core        ↓net_device（lan1~lanX）
```

----------

# 🔁 2. Driver Probe Flow

```
driver probe()
  ↓
init hardware（reset / regmap）
  ↓
detect switch（chip id）
  ↓
setup dsa_switch
  ↓
dsa_register_switch()
  ↓
DSA core 建立 port
  ↓
產生 net_device（lan1~lanX）
```

----------

## 📌 關鍵 API

```
dsa_register_switch(ds);
```

👉 這一行是：

```
把 switch 註冊到 DSA core
```

----------

# 🧠 3. `dsa_switch` 初始化

```
struct dsa_switch *ds;

ds->ops = &my_switch_ops;
ds->num_ports = 5;
ds->priv = priv;
```

----------

## 👉 重點

```
num_ports = switch port 數量（含 CPU port）
```


# 🔧 4. `dsa_switch_ops`


## 最基本：

```
static const struct dsa_switch_ops ops = {
    .setup         = my_setup,
    .port_enable   = my_port_enable,
    .port_disable  = my_port_disable,
};
```

----------

## 常見擴充：

```
    .phy_read
    .phy_write
    .get_tag_protocol
```

# 🔌 5. 與硬體的連接方式


## 🔹 MDIO-based switch

```
MAC
 ↓
MDIO
 ↓
Switch chip
```

👉 常見：

-   Realtek
-   Marvell


## 🔹 SPI-based switch

```
CPU
 ↓
SPI
 ↓
Switch chip
```

👉 常見：

-   工業控制
-   小型 switch

## 🔹 I2C-based
👉 driver probe 會是：

```
spi_driver
mdio_driver
i2c_driver
```


# 🧩 6. Port 初始化（DSA core）


DSA core 會做：

```
for each port:
    create dsa_port
    assign type（CPU / USER）
    建立 netdev（user port）
```

# 🧠 7. CPU port 設定

driver 要標記：

```
哪一個 port 是 CPU port
```

----------

👉 DTS 或 driver：

```
port@0 → CPU
port@1~4 → user
```


# 🔗 8. Tagging protocol 設定


```
.get_tag_protocol = my_tag_proto;
```

----------

👉 driver 告訴 kernel：

```
用哪種 tagging 格式
```

# 🔁 9. Packet flow


## TX

```
lan1
 ↓
DSA core
 ↓
tagging
 ↓
eth0
 ↓
MAC
```

----------

## RX

```
eth0  
 ↓  
driver 收到  
 ↓ 
DSA core 拆 tag  
 ↓
分發到 lanX
```


# 🧪 10. Debug


## ✔ driver probe

```
dmesg | grep -i switch
```

----------

## ✔ DSA 註冊

```
dmesg | grep dsa
```

----------

## ✔ netdev

```
ip link
```

----------

## ✔ driver info

```
ethtool -i lan1
```


# ❗ 11. 常見錯誤


## ❌ 沒有 lanX

👉 原因：

```
dsa_register_switch 沒成功
num_ports 錯
```


## ❌ port 沒 enable

👉 檢查：

```
.port_enable 沒實作
```


## ❌ 封包不通

👉 可能：

```
tagging 錯CPU port 設錯
```

## ❌ switch 沒反應

👉 檢查：

```
SPI / MDIO communication
chip id
```

# 🧠 12. Debug Flow


```
driver probe OK?
→ DSA 註冊 OK?
→ port 有沒有？
→ CPU port OK?
→ tagging OK?
```


# 🔍 13. Trace 建議


## ✔ driver log

```
pr_info("switch probe\n");
pr_info("port enable %d\n", port);
```

----------

## ✔ ftrace

```
echo net_dev_xmit > /sys/kernel/debug/tracing/set_event
```


# 🧠 14. 總結

```
switch driver = 初始化硬體 + 告訴 DSA core 怎麼用它
```


