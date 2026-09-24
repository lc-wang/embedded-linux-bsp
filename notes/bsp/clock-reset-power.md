# BSP Clock / Reset / Power（SoC 資源整合）

> 本章定位：
> 
> -   站在 **BSP / SoC Bring-up Engineer** 視角，理解 clock、reset、power 如何影響整個系統
>     
> -   說清楚為什麼 driver probe 失敗、裝置偶發失效，往往不是 driver bug
>     
> -   能實際用於 debug：probe defer、裝置不穩、suspend/resume 失效
>     

## 1. 為什麼 Clock / Reset / Power 是 BSP 的核心

在 SoC 系統中：
-   driver 只是「使用者」
-   clock / reset / power 才是「資源提供者」

如果資源狀態錯誤：
-   driver 就算寫得完全正確，也無法正常運作   

**BSP bring-up 的本質，就是把這些資源接對、接齊。**

## 2. Clock Framework：裝置是否能跑的前提

### 2.1 Clock 在系統中的角色

Clock 決定：
-   裝置是否能運作
-   裝置的效能與功耗

Linux 使用 Common Clock Framework（CCF）管理：
-   clock tree
-   clock enable / disable 
-   clock rate

### 2.2 常見 BSP 問題

-   clock 沒 enable
-   parent clock 選錯
-   rate 不符合硬體需求

結果常見表現：

-   driver probe 成功，但功能異常 
-   裝置偶爾失效

## 3. Reset Controller：初始化順序的關鍵

### 3.1 Reset 的角色

Reset 決定：
-   裝置是否處於已知狀態
-   是否能安全開始操作

SoC 通常提供：
-   多個 reset line 
-   不同 reset domain

### 3.2 常見錯誤

-   reset 沒 release
-   reset 順序錯誤
-   reset 與 clock 時序不對

**reset 問題通常表現為「probe 就卡住」。**

## 4. Power / Regulator：穩定性的基礎

### 4.1 Regulator Framework

Regulator 決定：

-   電壓是否存在   
-   電壓是否穩定

driver 透過 regulator API：

-   取得電源
-   啟用 / 停用    

### 4.2 BSP 常見問題

-   regulator 未宣告  
-   enable 順序錯誤
-   voltage 不符合 datasheet

結果可能是：

-   裝置偶發性錯誤
-   suspend / resume 後失效

## 5. Device Tree 中的 Clock / Reset / Power

### 5.1 DTS 是描述，不是行為

DTS 負責：
-   宣告資源關係    

DTS **不負責**：
-   確保初始化順序
-   確保資源可用時機

這些都由 driver model 與 framework 處理。

### 5.2 DTS 常見陷阱

-   clock-names 不一致
-   reset line 遺漏
-   regulator phandle 錯誤   

**DTS 看起來對，不代表資源真的 ready。**

## 6. Probe Defer 與資源依賴

當 driver 回傳：

```c
return -EPROBE_DEFER;
```

代表：

> 資源尚未就緒（clock / reset / regulator）

這是 BSP 中：
-   正常且必要的行為

## 7. Suspend / Resume 為什麼容易壞

Suspend / resume 需要：
-   正確的 power sequence
-   正確的 clock / reset handling

常見問題：
-   resume 後裝置無回應
-   clock 未重新 enable

**這通常是 BSP 整合問題，不是單一 driver bug。**

## 8. 常見問題與排查（Debug Checklist）

### 8.1 Clock

-   是否 enable
-   parent 是否正確
-   rate 是否合理

### 8.2 Reset

-   是否已 release
-   順序是否正確

### 8.3 Power

-   regulator 是否存在
-   voltage 是否穩定
