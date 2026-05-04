
## 🧠 Ethernet Device Tree（DTS）關鍵設定

本章節重點：

-   MAC / PHY DTS 結構
-   `phy-mode`（實戰重點）
-   RGMII delay（最常踩坑）
-   MDIO / PHY 定義
-   常見錯誤與 debug

----------

## 🧩 1. 基本 DTS 架構

```
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

----------

## 🔗 2. MAC ↔ PHY 關係

```
MAC driver
   ↓
phy-handle
   ↓
PHY node（MDIO）
```

👉 重點：

```
phy-handle = <&phy0>;
```

----------

## 🔌 3. MDIO bus 描述

```
mdio {
    #address-cells = <1>;
    #size-cells = <0>;

    phy0: ethernet-phy@1 {
        reg = <1>;
    };
};
```

----------

### 📌 關鍵

```
reg = <1>  → PHY address（硬體 strap）
```

👉 如果錯：

```
No PHY found
```

----------

## 🧠 4. phy-mode

```
phy-mode = "rgmii";
```

----------

### 常見值

```
mii
rmii
rgmii
rgmii-id
rgmii-txid
rgmii-rxid
sgmii
```

----------

## ⚠️ 5. RGMII delay

### 問題本質：

```
clock 與 data 必須有 timing skew（約 2ns）
```

----------

## 🔥 RGMII mode 差異（一定要記） 
  
| mode | TX delay | RX delay |  
|-------------|----------|----------|  
| rgmii | ❌ | ❌ |  
| rgmii-id | ✅ | ✅ |  
| rgmii-txid | ✅ | ❌ |  
| rgmii-rxid | ❌ | ✅ |

----------

## 🧠 誰負責 delay？

```
MAC？
PHY？
PCB？
```

👉 三種可能：


| 情境 | 誰做 delay | 建議 mode |  
|----------------------|------------|-----------|  
| PHY 有 internal delay | PHY | rgmii-id |  
| MAC driver 做 | MAC | rgmii |  
| PCB trace | PCB | rgmii |

----------

## ❗ 最常見錯誤


### ❌ link up 但 ping 不通

```
👉 RGMII delay 錯
```

----------

### ❌ intermittent packet loss

```
👉 skew 不穩
```

----------

## 🧪 6. 實戰 DTS 範例


### ✔ stmmac（常見）

```
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

----------

### ✔ fec（NXP）

```
fec@... {
    phy-mode = "rgmii";

    phy-handle = <&phy0>;
};
```

👉 FEC 常常：

```
delay 在 MAC
```

----------

## 🔧 7. reset / power / clock


### PHY reset

```
reset-gpios = <&gpio1 5 GPIO_ACTIVE_LOW>;
reset-assert-us = <10000>;
reset-deassert-us = <10000>;
```

----------

### clock

```
clocks = <&clk ...>;
```

----------

## 🧪 8. Debug Flow


### ✔ Step 1：interface 有沒有？

```
ip link
```

----------

### ✔ Step 2：PHY 有沒有？

```
dmesg | grep phy
```

----------

### ✔ Step 3：link 狀態

```
ethtool eth0
```

----------

### ✔ Step 4：MDIO

```
mdio-tool dump eth0 1
```

----------

### ✔ Step 5：封包

```
pingtcpdump -i eth0
```

----------

## ❗ 9. 常見錯誤

----------

### ❌ PHY address 錯

```
No PHY found
```

----------

### ❌ phy-mode 錯

```
link up but no traffic
```

----------

### ❌ RGMII delay 錯

```
packet drop / CRC error
```

----------

### ❌ reset 沒設

```
PHY 不穩 / 抓不到
```

----------

### ❌ clock 沒開

```
MDIO timeout
```

----------

## 🧠 10. BSP Debug 思維

你要分層：

```
DTS 問題？
PHY 問題？
MAC driver 問題？
```

----------

### 快速判斷

| 現象           | 問題            |
|----------------|-----------------|
| 沒 eth0        | Driver          |
| PHY 抓不到     | DTS / MDIO      |
| link 不上      | PHY             |
| link 上但不通  | RGMII           |


