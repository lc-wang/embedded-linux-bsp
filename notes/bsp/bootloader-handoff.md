# BSP Bootloader ↔ Kernel Handoff（初始化責任交界實務）

> 本章定位：
> 
> -   站在 **BSP / SoC Bring-up Engineer** 視角，釐清 Bootloader（常見為 U-Boot）與 Linux Kernel 之間的初始化責任邊界
>     
> -   說清楚為什麼「Kernel 看起來壞掉」其實常是 bootloader 遺留狀態問題
>     
> -   能實際用於 debug：clock 不對、pin 狀態怪、記憶體配置錯誤
>     

## 1. 為什麼 Bootloader ↔ Kernel 交界是 BSP 的灰色地帶

在 BSP bring-up 中，bootloader 與 kernel 的關係常被簡化為：

> Bootloader 啟動 → Kernel 接手

但實務上，它們之間存在一個**責任交界層**：
-   有些資源由 bootloader 初始化
-   kernel 假設這些狀態已存在或會被重設 

**當雙方假設不一致，就會產生極難判斷的 BSP 問題。**

## 2. Kernel 對 Bootloader 的基本假設

Linux kernel 在設計上假設：

-   CPU 已進入正確 execution level 
-   基本記憶體可用
-   device tree 正確描述硬體

但 kernel **不保證**：

-   clock 一定處於預期狀態
-   pinmux 一定是乾淨初始值
-   power domain 一定被 reset

## 3. Bootloader 常做、但 Kernel 未必會重設的事情

### 3.1 Clock / PLL 初始化

bootloader 常為了：

-   console 
-   DRAM
-   storage

而設定 clock / PLL。

若 kernel：

-   假設 clock 為 reset state
-   卻實際繼承 bootloader 狀態

可能造成 clock tree 行為不一致。

### 3.2 Pinmux / GPIO 狀態

bootloader 常設定：

-   UART  
-   storage

但 kernel pinctrl driver：

-   可能不會完整覆寫所有 pin

結果：

-   pin 狀態殘留
-   功能偶發失效

### 3.3 記憶體與保留區域

bootloader 可能：

-   carve out reserved memory
-   放置 firmware / logo  

若 DTS 未同步：
-   kernel 可能覆寫這些區域

## 4. Device Tree 作為交界契約

### 4.1 DTS 的角色

Device Tree 是：
-   bootloader 與 kernel 的**共享描述檔**

它應該描述：

-   硬體結構
-   可用資源 

而不是：
-   當前 runtime 狀態

### 4.2 常見 DTS 同步問題

-   bootloader DTS 與 kernel DTS 不一致
-   clock / pinctrl node 定義不同

**這是 BSP 專案中非常常見的隱性 bug 來源。**

## 5. 為什麼問題常在 Kernel 才爆出來

原因在於：
-   bootloader 只跑一次
-   kernel 需要處理 runtime / suspend / resume

如果初始狀態不乾淨：
-   問題會在後期才出現

## 6. Debug Bootloader ↔ Kernel Handoff 的實務方法

### 6.1 驗證 kernel 是否繼承 bootloader 狀態

```bash
cat /proc/cmdline
```

確認：
-   是否使用正確 DTB
-   bootargs 是否影響系統行為

### 6.2 比對 bootloader 與 kernel DTS

-   確認 clock / pinctrl / memory node 一致
-   避免雙方 DTS 漂移

### 6.3 刻意在 bootloader 關閉初始化

實務 debug 時：
-   嘗試關閉 bootloader 某些初始化 
-   觀察 kernel 行為是否改變

可快速定位責任歸屬。

## 7. 常見問題與排查（常見誤判與 Debug）

| 現象           | 常見誤判        | 真正原因              |
|----------------|-----------------|-----------------------|
| Kernel clock 怪| Clock driver    | Bootloader 狀態殘留  |
| Pin 行為不一致 | Driver bug      | Pinmux 設定殘留      |
| 隨機 Crash     | Kernel bug      | Memory carve-out 問題|
