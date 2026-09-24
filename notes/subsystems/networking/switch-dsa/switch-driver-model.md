# Switch Driver Model

本章節重點：

-   switch driver 的 probe flow
-   `dsa_register_switch()` 做了什麼
-   driver 要實作哪些 ops
-   與 MDIO / SPI / I2C 的關係

## 1. Driver 在整體架構的位置

```text
Platform / SPI / MDIO driver        ↓Switch driver（DSA）        ↓DSA core        ↓net_device（lan1~lanX）
```

## 2. Driver Probe Flow

```text
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

### 2.1 關鍵 API

```c
dsa_register_switch(ds);
```

這一行是：

```text
把 switch 註冊到 DSA core
```

## 3. `dsa_switch` 初始化

```c
struct dsa_switch *ds;

ds->ops = &my_switch_ops;
ds->num_ports = 5;
ds->priv = priv;
```

### 3.1 重點

```text
num_ports = switch port 數量（含 CPU port）
```

## 4. `dsa_switch_ops`

### 4.1 最基本

```c
static const struct dsa_switch_ops ops = {
    .setup         = my_setup,
    .port_enable   = my_port_enable,
    .port_disable  = my_port_disable,
};
```

### 4.2 常見擴充

```text
    .phy_read
    .phy_write
    .get_tag_protocol
```

## 5. 與硬體的連接方式

### 5.1 MDIO-based switch

```text
MAC
 ↓
MDIO
 ↓
Switch chip
```

常見：

-   Realtek
-   Marvell

### 5.2 SPI-based switch

```text
CPU
 ↓
SPI
 ↓
Switch chip
```

常見：

-   工業控制
-   小型 switch

### 5.3 I2C-based

driver probe 會是：

```text
spi_driver
mdio_driver
i2c_driver
```

## 6. Port 初始化（DSA core）

DSA core 會做：

```text
for each port:
    create dsa_port
    assign type（CPU / USER）
    建立 netdev（user port）
```

## 7. CPU port 設定

driver 要標記：

```text
哪一個 port 是 CPU port
```

DTS 或 driver：

```text
port@0 → CPU
port@1~4 → user
```

## 8. Tagging protocol 設定

```c
.get_tag_protocol = my_tag_proto;
```

driver 告訴 kernel：

```text
用哪種 tagging 格式
```

## 9. Packet flow

### 9.1 TX

```text
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

### 9.2 RX

```text
eth0  
 ↓  
driver 收到  
 ↓ 
DSA core 拆 tag  
 ↓
分發到 lanX
```

## 10. Debug

### 10.1 driver probe

```bash
dmesg | grep -i switch
```

### 10.2 DSA 註冊

```bash
dmesg | grep dsa
```

### 10.3 netdev

```text
ip link
```

### 10.4 driver info

```bash
ethtool -i lan1
```

## 11. 常見問題與排查

### 11.1 沒有 lanX

原因：

```text
dsa_register_switch 沒成功
num_ports 錯
```

### 11.2 port 沒 enable

檢查：

```text
.port_enable 沒實作
```

### 11.3 封包不通

可能：

```text
tagging 錯CPU port 設錯
```

### 11.4 switch 沒反應

檢查：

```text
SPI / MDIO communication
chip id
```

## 12. Debug Flow

```text
driver probe OK?
→ DSA 註冊 OK?
→ port 有沒有？
→ CPU port OK?
→ tagging OK?
```

## 13. Trace 建議

### 13.1 driver log

```c
pr_info("switch probe\n");
pr_info("port enable %d\n", port);
```

### 13.2 ftrace

```bash
echo net_dev_xmit > /sys/kernel/debug/tracing/set_event
```

## 14. 總結

```text
switch driver = 初始化硬體 + 告訴 DSA core 怎麼用它
```
