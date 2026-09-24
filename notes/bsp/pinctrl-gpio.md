# BSP Pin Control & GPIO（腳位複用與控制整合實務）

> 本章定位：
> 
> -   站在 **BSP / SoC Bring-up Engineer** 視角，理解 pinctrl 與 GPIO 在系統中的責任邊界
>     
> -   說清楚為什麼「DTS 看起來對，但功能就是不動」常常是 pinmux 問題
>     
> -   能實際用於 debug：I2C 掃不到、IRQ 不觸發、Audio 沒聲音、wakeup 失效
>     

## 1. 為什麼 Pin Control 是 BSP 的高風險區

在 BSP bring-up 中，pinctrl 是**出錯率極高、但又最容易被忽略**的一層：
-   同一個 pin 可能有多個功能（GPIO / I2C / SPI / I2S / PWM）
-   不同狀態下需要不同 pin 設定（boot / runtime / suspend）
-   問題常表現為「完全沒反應」，而不是明確錯誤

**pinctrl 問題通常不會讓 probe 失敗，但會讓功能失效。**

## 2. Pin Control 與 GPIO 的責任邊界

### 2.1 pinctrl 是「複用與電氣設定」

pinctrl 負責：
-   功能選擇（mux）   
-   電氣屬性（pull-up/down、drive strength）

它回答的是：

> 這個 pin 現在「是什麼功能、怎麼接電氣」。

### 2.2 GPIO 是「邏輯控制」

GPIO 負責：
-   input / output
-   value / direction

它假設：

> pin 已經被正確設定成 GPIO 功能。

**GPIO API 正確，不代表 pinctrl 設定正確。**

## 3. DTS 中 pinctrl 的基本結構

在 DTS 中，pinctrl 通常分成兩層：
-   pin configuration node（SoC-specific）
-   device node 中引用 pinctrl state 

```dts
&pinctrl {
    i2c1_pins: i2c1-pins {
        pins = "PIN_A", "PIN_B";
        function = "i2c";
        bias-pull-up;
    };
};

&i2c1 {
    pinctrl-names = "default";
    pinctrl-0 = <&i2c1_pins>;
};
```

關鍵不是語法，而是**狀態的切換時機**。

## 4. pinctrl state 與生命週期

### 4.1 default / sleep state

常見 state：
-   `default`
-   `sleep`

系統在：
-   driver probe 時套用 default
-   suspend 時切到 sleep

若 sleep state 缺失：
-   resume 後功能可能異常

### 4.2 runtime 切換

部分裝置：
-   需要在 runtime 切換 pin 狀態 

若 driver 未處理：
-   功耗異常
-   偶發功能失效

## 5. 常見 BSP 失敗模式

### 5.1 I2C 掃不到裝置

可能原因：
-   SDA / SCL 沒被 mux 成 I2C 
-   pull-up 設定錯誤

### 5.2 IRQ 永遠不觸發

可能原因：
-   pin 被設定成 output
-   沒有正確設定 input / pull

### 5.3 Audio 沒聲音

可能原因：
-   I2S pin 沒被切到 audio function   
-   sleep state 切換錯誤

## 6. pinctrl 與 suspend / resume

suspend/resume 期間：
-   pin 可能被重新配置
-   預期的電氣狀態可能被改變

若 sleep state 不完整：
-   resume 後裝置「看起來活著，但實際不工作」

## 7. Pin Control Debug Toolbox

### 7.1 檢查 pinctrl 是否套用成功

```bash
ls /sys/kernel/debug/pinctrl/
```

查看對應 controller：

```bash
cat /sys/kernel/debug/pinctrl/*/pinmux-pins
```

### 7.2 檢查 pin 的目前狀態

```bash
cat /sys/kernel/debug/pinctrl/*/pins
```

觀察重點：

-   pin 是否被 claimed
-   目前 function 為何

### 7.3 GPIO 層確認

```bash
gpioinfo
gpioset
gpioget
```

若 GPIO 操作正常但功能不對：
-   幾乎一定是 pinctrl 問題

### 7.4 suspend / resume 專用檢查

```bash
echo mem > /sys/power/state
```

resume 後再次檢查 pin 狀態，確認是否切回 default。

## 8. 常見問題與排查（常見誤判與 Debug）

| 現象           | 常見誤判      | 真正原因        |
|----------------|---------------|-----------------|
| GPIO API 正常  | Driver bug    | Pinmux 錯誤     |
| Probe 成功     | 硬體 OK       | Pin state 錯誤  |
| Resume 壞掉    | PM 問題       | Sleep pinctrl   |
