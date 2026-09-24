# Ethernet Device Tree（DTS）關鍵設定

本章節重點：

-   MAC / PHY DTS 結構
-   `phy-mode`（實戰重點）
-   RGMII delay（最常踩坑）
-   MDIO / PHY 定義
-   常見錯誤與 debug

## 1. 基本 DTS 架構

```dts
ethernet@... {
    compatible = "...";

    phy-mode = "rgmii";
    phy-handle = <&phy0>;

    mdio {
        phy0: ethernet-phy@1 {
            reg = <1>;
        };
    };
};
```

## 2. MAC ↔ PHY 關係

```text
MAC driver
   ↓
phy-handle
   ↓
PHY node（MDIO）
```

重點：

```dts
phy-handle = <&phy0>;
```

## 3. MDIO bus 描述

```dts
mdio {
    #address-cells = <1>;
    #size-cells = <0>;

    phy0: ethernet-phy@1 {
        reg = <1>;
    };
};
```

### 3.1 關鍵

```text
reg = <1>  → PHY address（硬體 strap）
```

如果錯：

```text
No PHY found
```

## 4. phy-mode

```dts
phy-mode = "rgmii";
```

### 4.1 常見值

```text
mii
rmii
rgmii
rgmii-id
rgmii-txid
rgmii-rxid
sgmii
```

## 5. RGMII delay

### 5.1 問題本質

```text
clock 與 data 必須有 timing skew（約 2ns）
```

### 5.2 RGMII mode 差異（一定要記）

| mode | TX delay | RX delay |  
|-------------|----------|----------|  
| rgmii | ✗ | ✗ |  
| rgmii-id | ✓ | ✓ |  
| rgmii-txid | ✓ | ✗ |  
| rgmii-rxid | ✗ | ✓ |

### 5.3 誰負責 delay？

```text
MAC？
PHY？
PCB？
```

三種可能：

| 情境 | 誰做 delay | 建議 mode |  
|----------------------|------------|-----------|  
| PHY 有 internal delay | PHY | rgmii-id |  
| MAC driver 做 | MAC | rgmii |  
| PCB trace | PCB | rgmii |

### 5.4 最常見錯誤

#### link up 但 ping 不通

```text
RGMII delay 錯
```

#### intermittent packet loss

```text
skew 不穩
```

## 6. 實戰 DTS 範例

### 6.1 stmmac（常見）

```dts
ethernet@... {
    compatible = "snps,dwmac";

    phy-mode = "rgmii-id";
    phy-handle = <&phy0>;

    mdio {
        phy0: ethernet-phy@1 {
            reg = <1>;
        };
    };
};
```

### 6.2 fec（NXP）

```dts
fec@... {
    phy-mode = "rgmii";

    phy-handle = <&phy0>;
};
```

FEC 常常：

```text
delay 在 MAC
```

## 7. reset / power / clock

### 7.1 PHY reset

```dts
reset-gpios = <&gpio1 5 GPIO_ACTIVE_LOW>;
reset-assert-us = <10000>;
reset-deassert-us = <10000>;
```

### 7.2 clock

```dts
clocks = <&clk ...>;
```

## 8. Debug Flow

### 8.1 Step 1：interface 有沒有？

```text
ip link
```

### 8.2 Step 2：PHY 有沒有？

```bash
dmesg | grep phy
```

### 8.3 Step 3：link 狀態

```bash
ethtool eth0
```

### 8.4 Step 4：MDIO

```text
mdio-tool dump eth0 1
```

### 8.5 Step 5：封包

```text
pingtcpdump -i eth0
```

## 9. 常見問題與排查

### 9.1 PHY address 錯

```text
No PHY found
```

### 9.2 phy-mode 錯

```text
link up but no traffic
```

### 9.3 RGMII delay 錯

```text
packet drop / CRC error
```

### 9.4 reset 沒設

```text
PHY 不穩 / 抓不到
```

### 9.5 clock 沒開

```text
MDIO timeout
```

## 10. BSP Debug 思維

你要分層：

```text
DTS 問題？
PHY 問題？
MAC driver 問題？
```

### 10.1 快速判斷

| 現象           | 問題            |
|----------------|-----------------|
| 沒 eth0        | Driver          |
| PHY 抓不到     | DTS / MDIO      |
| link 不上      | PHY             |
| link 上但不通  | RGMII           |
