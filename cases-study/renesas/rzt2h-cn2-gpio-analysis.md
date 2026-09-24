# RZ/T2H CN2 GPIO / PFC / Pinmux 分析報告

## 1. 問題概述

在 RZ/T2H Evaluation Board 上，**CN2 header（pin 11–14）** 對應到 SoC 的 **P03_x 腳位**。這組腳位在硬體文件與 firmware enum 中，顯示其具備多種 peripheral 功能（ENCIF / GPT / MTU / I2C / IRQ…）。

然而，在實際系統整合時，會遇到以下疑問：

-   TRM / firmware enum 中 **沒有看到 GPIO mode**
    
-   但 Linux DTS 卻可以寫：
    
    ```dts
    function = "gpio";
    ```
    
-   GPIO、PFC、pinmux 三者的關係為何？
- pull-up 與 open-drain 的差異是什麼？

----------

## 2. 分析過程

### 2.1 RZ/T2H 腳位控制架構

#### 2.1.1 兩層控制模型

RZ/T2H（以及多數 Renesas Linux SoC）的腳位控制可分為兩層：

```
Peripheral Block (ENCIF / I2C / GPT / MTU …)
        ▲
        │（內部訊號）
        │
Pin Function Controller (PFC / PMC / PM)
        ▲
        │
Physical Pin (P03_3 → CN2 pin 11)
```

Peripheral block 負責功能邏輯（例如 encoder、timer、I2C），而 **PFC（Pin Function Controller）** 則負責決定外部實體腳位要連接到哪個內部功能。

----------

#### 2.1.2 關鍵暫存器角色

| Register | 功能說明                                              |
|----------|-------------------------------------------------------|
| PMC      | Peripheral Module Control（0 = GPIO，1 = Peripheral） |
| PFC      | Peripheral Function Select（僅在 PMC = 1 時有效）     |
| PM       | GPIO Input / Output 模式設定                           |
| P        | GPIO Output Value                                     |
| DRCTL    | Drive Control（輸出驅動型態 / 強度）                  |

----------

### 2.2 為什麼 TRM / firmware enum 沒有 GPIO？

#### 2.2.1 firmware enum 的實際意義

以 P03_3 為例，firmware 中可看到類似以下定義：

```c
IOPORT_PIN_P033_PFC_00_IRQ13
IOPORT_PIN_P033_PFC_04_D11
IOPORT_PIN_P033_PFC_17_IIC_SCL1
IOPORT_PIN_P033_PFC_22_ENCIFCK02
```

這些 enum 並不是「腳位所有可用模式」，而是：

> **當 PMC = 1（Peripheral 模式）時，PFC 可選擇的 peripheral multiplexer 值**。

----------

#### 2.2.2 GPIO 為什麼不在 enum 裡？

在 Renesas 架構中：

-   **GPIO 不是 PFC 的一個選項**
-   GPIO 是「PMC = 0」的狀態
-   一旦 PMC = 0：
    -   PFC 完全不參與
    -   腳位由 GPIO controller 直接控制

| 模式        | PMC | PFC                    |
|-------------|-----|------------------------|
| GPIO        | 0   | 不使用                 |
| Peripheral  | 1   | 決定 ENCIF / I2C / GPT |


因此，TRM 或 firmware enum 中 **不會、也不需要列出 GPIO**。

----------

### 2.3 Linux DTS 為什麼可以寫 `function = "gpio"`？

#### 2.3.1 Renesas pinctrl binding 定義

在 `Documentation/devicetree/bindings/pinctrl/renesas,pfc.yaml` 中，Renesas PFC binding 明確允許：

```dts
function = "gpio";
```

此屬於標準 pinctrl client node 的合法設定。

----------

#### 2.3.2 `function = "gpio"` 的實際語意

在 Linux pinctrl driver 中：

```
function = "gpio"
→ Disable peripheral function
→ Set PMC = 0
→ Pin becomes GPIO
```

這與 firmware 中「GPIO 分支」的行為在硬體層級上完全一致。

----------

### 2.4 pull-up 與 open-drain 的正確理解

#### 2.4.1 `bias-pull-up` 的意義

```dts
bias-pull-up;
```

代表啟用 SoC 內部 weak pull-up（通常為數十 kΩ 等級），僅影響腳位在未被驅動時的預設電位。

----------

#### 2.4.2 open-drain 的實際行為

open-drain 的本質為：

-   輸出端不主動拉高
-   僅能拉低或呈現 Hi-Z
-   必須搭配 pull-up 才能形成高電位
    
在 RZ/T2H 上：

-   GPIO controller 沒有獨立的硬體 open-drain mode
-   常見作法為：
    -   拉低：GPIO output low
    -   釋放：GPIO input（Hi-Z） 
-   pull-up 可使用：
    -   外接電阻（建議）
    -   SoC 內部 pull-up（僅適合低速或非嚴格需求）

----------

## 3. 解決方案

### 3.1 CN2 腳位是否可以改成 GPIO？

#### 3.1.1 硬體角度

以 CN2 pin 11（P03_3）為例：

-   支援多種 peripheral（ENCIF / IIC / GPT / IRQ）    
-   GPIO 是該腳位的基本模式
-   不需要 enum、不需要 PFC

**硬體上完全可行**。

----------

#### 3.1.2 Linux DTS 建議寫法

```dts
&pinctrl {
        cn2_gpio_pins: cn2-gpio-pins {
                pins = "P03_3", "P03_4", "P03_5", "P03_6"; /* CN2 11–14 */
                function = "gpio";
                bias-disable;        /* 或 bias-pull-up 視需求 */
        };
};
```

此設定的效果為：

-   PMC = 0
-   清除任何 peripheral mux
-   腳位強制回到 GPIO 模式

----------

## 4. 結論與建議

### 4.1 目前 DTS 狀態的結論

-   現有 DTS **未明確定義 CN2 11–14 為 GPIO**
    
-   未宣告 ≠ GPIO
-   未宣告時，腳位行為取決於：
    -   bootloader 預設
    -   是否有其他 subsystem 啟用該 pinmux
        
**若要穩定使用 GPIO，必須在 pinctrl 中明確宣告。**

----------

### 4.2 總結

-   GPIO 不是 PFC enum 的一種 
-   GPIO = PMC = 0
-   PFC enum 僅用於 peripheral 模式
-   Linux DTS `function = "gpio"` 合法且正確
-   CN2 / P03_x 腳位硬體上完全可作 GPIO
-   pull-up 與 open-drain 概念必須分開理解
-   為避免不確定性，DTS 應明確宣告 pinmux ownership
