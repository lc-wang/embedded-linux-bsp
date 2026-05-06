
## 🧠 DSA Device Tree

本章節重點：

-   DSA 的 DTS 架構
-   CPU port / user port 定義
-   PHY / fixed-link 差異

----------

# 🧩 1. 基本 DSA DTS 架構

```
switch@0 {
    compatible = "...";

    ports {
        port@0 {
            reg = <0>;
            label = "cpu";
            ethernet = <&eth0>;
            phy-mode = "rgmii";
        };

        port@1 {
            reg = <1>;
            label = "lan1";
            phy-handle = <&phy1>;
        };

        port@2 {
            reg = <2>;
            label = "lan2";
            phy-handle = <&phy2>;
        };
    };
};
```


# 🔌 2. Port 類型


## 🔹 CPU port

```
port@0 {
    reg = <0>;
    label = "cpu";

    ethernet = <&eth0>;   // 🔥 關鍵
    phy-mode = "rgmii";
};
```


### 📌 重點

```
ethernet = <&eth0>
```

👉 代表：

```
這個 port 連到 MAC（CPU）
```


## 🔹 User port

```
port@1 {
    reg = <1>;
    label = "lan1";

    phy-handle = <&phy1>;
};
```

----------

👉 代表：

```
這個 port 對應外部 PHY
```

----------

# 🧠 3. PHY 定義

## 🔹 外部 PHY

```
mdio {
    phy1: ethernet-phy@1 {
        reg = <1>;
    };
};
```

----------

👉 user port：

```
phy-handle = <&phy1>;
```

## 🔹 內建 PHY（switch 內）

👉 有些 switch 不需要外部 PHY node

----------

# 🔗 4. fixed-link

## 🔥 CPU port 常見寫法

```
port@0 {
    reg = <0>;
    label = "cpu";

    ethernet = <&eth0>;

    fixed-link {
        speed = <1000>;
        full-duplex;
    };
};
```

----------

### 📌 為什麼用 fixed-link？

```
CPU ↔ Switch 不做 auto-negotiation
```

----------

👉 否則：

```
link 不穩 / negotiation 卡住
```

----------

# ⚠️ 5. phy-mode

```
phy-mode = "rgmii-id";
```

----------

👉 這會直接影響：

```
CPU port timing（所有 lanX 都靠這條）
```

----------

## ❗ 超重要

```
CPU port timing 錯 = 所有 lan port 壞
```

----------

# 🧪 6. 完整範例

```
&eth0 {
    phy-mode = "rgmii-id";
};

switch@0 {
    compatible = "...";

    ports {
        port@0 {
            reg = <0>;
            label = "cpu";
            ethernet = <&eth0>;
            phy-mode = "rgmii-id";

            fixed-link {
                speed = <1000>;
                full-duplex;
            };
        };

        port@1 {
            reg = <1>;
            label = "lan1";
            phy-handle = <&phy1>;
        };

        port@2 {
            reg = <2>;
            label = "lan2";
            phy-handle = <&phy2>;
        };
    };
};
```

----------

# ❗ 7. 錯誤


## 🚨 Case 1：沒有 lan1~lan4

👉 原因：

```
ports node 錯
reg 錯
DSA 沒 parse 到
```


## 🚨 Case 2：lan link up 但不通

👉 90%：

```
CPU port phy-mode / RGMII delay 錯
```

## 🚨 Case 3：eth0 OK，但 lanX 不通

👉 檢查：

```
CPU port DTS（fixed-link / phy-mode）
```

## 🚨 Case 4：完全沒 link

👉 檢查：

```
phy-handle
MDIO
reset
```


## 🚨 Case 5：intermittent

👉 通常：

```
clock / delay / reset timing
```

----------

# 🔍 8. Debug 

## ✔ port 有沒有出現

```
ip link
```

----------

## ✔ link 狀態

```
ethtool lan1
```

----------

## ✔ CPU port

```
ethtool eth0
```

----------

## ✔ dmesg

```
dmesg | grep dsa
```

----------

# 🧠 9. Debug Flow

```
eth0 OK？
 → yes

lan1 不通？
 → CPU port DTS
 → phy-mode
 → fixed-link
```

----------

# 🔥 10. 觀念


## ✔ Rule 1

```
CPU port = 所有 port 的出口
```

----------

## ✔ Rule 2

```
CPU port timing 錯 = 全滅
```

----------

## ✔ Rule 3

```
fixed-link 很常是必要的
```

