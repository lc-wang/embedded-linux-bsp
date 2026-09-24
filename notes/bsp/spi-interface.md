# SPI Interface Bring-up & Debug Playbook

> 目的：
> 
> -   將 **SPI 從 Linux bus 架構 → Device Tree → driver → 實際硬體訊號** 串成一條可 debug 的工程路徑

## 1. SPI 在 BSP 中的角色定位

與 I2C 相比，SPI 在 BSP 的定位完全不同：

| Bus | 典型角色          | 工程特性                                   |
|-----|-------------------|--------------------------------------------|
| I2C | Control / Register| 低速、可容錯、對除錯相對友善               |
| I2S | Streaming         | 固定時序、Pipeline 清楚、連續資料流        |
| SPI | Data + Command    | 高頻、無容錯、對 Timing 極度敏感           |

SPI 常見工程現實：

-   多數裝置 **沒有 IRQ**（只能 polling / busy pin）
-   **CS / reset / dc-gpio** 與資料正確性強烈耦合
-   clock 一跑就錯，卻不一定會報錯

結果就是：

> _driver 看起來完全正常，但裝置永遠沒反應_

## 2. Linux SPI 架構

### 2.1 核心物件

```text
spi_controller (spi_master)
│
spi_device
│
spi_driver
```
-   `spi_controller`：SoC SPI IP（clock、CS、FIFO、DMA）
-   `spi_device`：DT 解析後的裝置實例
-   `spi_driver`：你寫的 driver    

### 2.2 資料實際怎麼送出去

```text
driver
	└─ spi_sync()
		└─ spi_controller->transfer_one()
			└─ HW shift out (CLK / MOSI / CS)
```
**Debug 重點不是 API，而是：**

-   CS 是 **controller 控** 還是 **GPIO 控**
-   transfer 是 sync 還是 async
-   clock mode / frequency 在哪裡決定    

## 3. Device Tree

> **SPI 問題，永遠先從 DT 看**。

### 3.1 Bus 與 Device 的基本結構

```dts
&spi2 {
	status = "okay";

	panel@0 {
		compatible = "vendor,eink-panel";
		reg = <0>; // chip select
		spi-max-frequency = <8000000>;
		spi-cpol;
		spi-cpha;

		reset-gpios = <&gpio3 5 GPIO_ACTIVE_LOW>;
		dc-gpios = <&gpio3 6 GPIO_ACTIVE_HIGH>;
		busy-gpios = <&gpio3 7 GPIO_ACTIVE_HIGH>;
	};
};
```

### 3.2 常見致命錯誤清單

| 錯誤項目                     | 典型現象描述                                   |
|------------------------------|------------------------------------------------|
| spi-max-frequency 設太高     | 傳輸看似正常，但資料內容錯誤                   |
| CPOL / CPHA 不符             | 裝置完全無反應                                 |
| CS 設定錯誤                  | Scope 看得到 Clock，但裝置未動作               |
| Reset / DC GPIO polarity 錯 | Init sequence 永遠失敗                         |

**DT 錯誤 = 100% driver debug 浪費時間**。

## 4. SPI Driver Bring-up 決策流程

```text
probe 進來了？
	├─ 否 → DT / compatible / bus
	└─ 是
		├─ reset sequence 是否正確？
		├─ spi_sync 是否被呼叫？
		├─ CS / CLK 是否真的動？
		├─ busy pin 是否變化？
		└─ timing 是否符合 datasheet？
```
**關鍵心法**：

> SPI bring-up 是「**同步驗證軟體與硬體**」，不是單純 debug code。

## 5. SPI Debug Toolbox

### 5.1 軟體側

-   `dev_dbg()` / dynamic debug（取代亂加 printk）
-   ftrace：
    -   `spi_sync`
    -   `spi_transfer_one`  
```bash
echo function_graph > current_tracer

echo spi_* > set_ftrace_filter
```
用途：確認 **driver → controller** 的路徑有沒有跑到。

### 5.2 硬體側

-   Logic Analyzer / Scope：
    -   CS 
    -   CLK
    -   MOSI -（必要時）MISO

**最重要判斷**：

-   CS 是否在正確時間被拉低
-   clock 是否穩定
-   data 是否對齊 clock edge

**只看 log，不看訊號 = SPI debug 一定失敗**。

## 6. Case Study：SPI 電子紙面板

### 6.1 為什麼 e‑ink 特別容易出問題

-   SPI 頻寬低、latency 高
-   更新需要 **multi-pass + waveform (LUT)**
-   busy pin 代表硬體狀態機

### 6.2 常見錯誤模式

| 現象                     | 真正原因                     |
|--------------------------|------------------------------|
| 有 Clock、沒畫面          | Update sequence 錯誤         |
| 第一張正常，之後卡住     | Busy pin 未正確等待          |
| 黑白正常、灰階不正確     | LUT / Pass 執行順序錯誤      |

### 6.3 關鍵工程決策

-   SPI 正確 ≠ 顯示正確    
-   顯示問題多半是 **狀態機與 timing**

## 7. 敘事

> 「SPI 問題一定先看 DT， 再用 scope 確認 CS/CLK/data， 確認 timing 沒問題後， 才回頭 trace spi_sync 的 call flow。」
