
# Linux Device Driver Model（裝置／驅動框架）

> 本章定位：
> 
> -   站在 **Linux / BSP Engineer** 視角，理解 Linux 如何用一套統一模型管理「裝置」與「驅動」
>     
> -   說清楚 driver 為什麼會 probe、為什麼 probe 失敗、為什麼順序會影響系統行為
>     
> -   能實際用於 debug：driver 沒起來、probe defer、suspend/resume 異常
>     

----------

## 1. 為什麼 Linux 需要 Device Driver Model

在真實系統中，Linux 面對的是：

-   裝置數量多、類型多（SoC IP、PCIe、USB、I2C、SPI）
-   裝置初始化順序不固定
-   有些資源（clock / regulator / firmware）必須「晚一點才到」
    

如果每個 driver 自己決定初始化時機，  
系統將變得不可預期。

**Device Driver Model 的目的不是方便寫 driver，  
而是讓整個系統能被「組織起來」。**

----------

## 2. 核心概念：device / driver / bus

Linux 用三個核心結構描述世界：

```text
struct device
struct device_driver
struct bus_type
```

這不是語法細節，而是系統架構。

----------

### 2.1 struct device：系統中的一個實體

-   每一個硬體或虛擬裝置，最終都對應一個 `struct device`
-   device 會出現在 sysfs（/sys/devices）
    

關鍵屬性：

-   parent / child
-   power management state
-   DMA / resource 關聯
    

**device 是「系統資源管理的單位」。**

----------

### 2.2 struct device_driver：能力的實作者

driver 表示：
-   「我知道怎麼控制某一類 device」
    

driver 定義：
-   probe()
-   remove()
-   suspend() / resume()
    

driver 本身不擁有裝置，只是「被配對」。

----------

### 2.3 struct bus_type：配對與仲裁者

bus 負責：
-   device 與 driver 的 match
-   probe 的時機與順序
    

常見 bus：
-   platform   
-   pci 
-   usb 
-   i2c / spi
    

**bus 是 driver model 的核心調度者。**

----------

## 3. Driver Probe 的實際流程

從系統角度看，一次 probe 流程如下：

```text
裝置出現（DT / ACPI / hotplug）
    ↓
bus 嘗試 match driver
    ↓
driver.probe()
    ↓
裝置初始化完成
```

### 3.1 為什麼 probe 順序重要

因為 driver 常依賴其他 subsystem：

-   clock    
-   regulator  
-   pinctrl  
-   firmware
    

如果資源尚未就緒：
-   probe 必須延後
    
----------

### 3.2 Deferred Probe（非常關鍵）

當 driver 回傳：

```c
return -EPROBE_DEFER;
```

代表：

> 「不是錯，是現在時機不對。」

Linux 會在資源到齊後重新嘗試。

**很多 BSP 問題其實是 probe defer 沒處理好。**

----------

## 4. Device Tree 與 Driver Model

在 SoC / BSP 世界：
-   Device Tree 描述 device    
-   Driver Model 決定何時 probe
    

### 4.1 DTS 的角色
-   DTS 建立 device node
-   kernel 依此建立 `struct device`
    
但：
-   DTS 不保證初始化順序
----------

### 4.2 為什麼 DTS 看起來對，driver 卻沒起來

常見原因：
-   clock / reset 沒 ready
-   pinctrl 尚未初始化
-   firmware 尚未可用
    

**這不是 DTS 錯，而是 Driver Model 的時序問題。**

----------

## 5. Power Management 與 Driver Model

### 5.1 suspend / resume 為什麼容易壞

因為：
-   suspend / resume 是「反向 probe」 
-   依賴 device hierarchy  

若 driver 未正確處理：
-   parent / child
    

就容易出現：
-   resume crash 
-   裝置失效

----------

### 5.2 Runtime PM

Runtime PM 讓 driver：
-   在不用時關閉裝置
-   在需要時重新啟動 

**這完全建立在 driver model 之上。**

----------

## 6. sysfs：觀察 Driver Model 的窗口

sysfs 不是 debug 工具，而是：

> **Driver Model 的即時狀態映射。**

重要路徑：
-   /sys/devices 
-   /sys/bus/* 
-   /sys/class
    
你可以用它來確認：
-   device 是否存在
-   driver 是否 bind

----------

## 7. BSP 常見問題與 Debug 心法

### 7.1 Driver 沒 probe，不要急著改 driver

先確認：
-   device 是否建立
-   bus 是否 match
-   是否發生 probe defer

----------

### 7.2 probe fail ≠ 系統錯誤

probe fail 可能代表：
-   硬體不存在
-   firmware 缺失
-   config 錯誤
    

**Driver Model 讓這些失敗變得可控。**
