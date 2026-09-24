# RZ/V2H SPI Timeout 問題分析與修正

## 1. 問題概述（問題背景）

在 Kakip 平台（Renesas RZ/V2H）上進行 SPI 通訊時，系統偶爾會出現 SPI timeout 的錯誤。

常見的 kernel log：
```text
spi_master spi0: transmit timeout  
spi_master spi0: receive timeout  
spidev spi0.0: SPI transfer failed: -110
```
此問題在進行連續 SPI 傳輸時容易發生，例如：

-   `spidev_test`

-   SPI device 測試（Pixpaper 電子紙）

這表示 SPI driver 在某些情況下沒有正確偵測到 **SPI 傳輸完成事件**，最終觸發 timeout。

## 2. 系統環境（測試平台）

| 項目 | 說明 |  
|------|------|  
| SoC | Renesas RZ/V2H |  
| 開發板 | Kakip |  
| SPI Controller | RSPI |  
| 作業系統 | Ubuntu |  
| SPI Device | Pixpaper 電子紙 |  
| 測試工具 | spidev_test / epd_test |

## 3. 除錯過程

### 3.1 問題重現

#### 3.1.1 spidev_test

使用 Linux SPI 測試工具即可重現此問題。
```bash
sudo ./spidev_test -D /dev/spidev0.0 -v -S 32 -I 100
```
在連續傳輸一段時間後，系統會出現 timeout：
```text
spi_master spi0: transmit timeout  
SPI transfer failed: -110
```
這代表 SPI controller 的傳輸沒有在預期時間內完成。

#### 3.1.2 SPI Device 測試（Pixpaper）

在 Pixpaper 電子紙測試程式中，也可以觀察到相同問題。

SPI 設定：
```text
SPI_SPEED = 10 MHz
```
執行時 log：
```text
spi_master spi0: receive timeout 0  
spidev spi0.0: SPI transfer failed: -110
```
雖然部分情況下顯示更新仍然成功，但頻繁的 timeout 表示 SPI 通訊存在不穩定情況。

### 3.2 SPI Driver 架構

RZ/V2H 使用 Linux kernel 中的 **RSPI driver**。

SPI 資料流如下：
```text
Userspace  
 ↓  
spidev  
 ↓  
SPI subsystem  
 ↓  
rspi driver  
 ↓  
RSPI controller  
 ↓  
SPI device
```
Driver 主要透過以下資訊判斷傳輸是否完成：

-   FIFO 狀態

-   interrupt

-   status flag

若 driver 對這些事件的處理不完整，就可能誤判傳輸狀態。

## 4. Root Cause 分析

問題的根本原因在於 **RSPI driver 對傳輸完成事件的處理不完整**。

原始 driver 主要依賴 FIFO 狀態，例如：

-   TX FIFO empty

-   RX FIFO full

來判斷 SPI 傳輸是否完成。

但實際上：

**FIFO 狀態並不一定代表 SPI 傳輸真正結束。**

在連續 SPI 傳輸時可能發生：

-   driver 等待錯誤的 FIFO 狀態

-   interrupt 未正確觸發

-   completion event 遺漏

最終 driver 等不到完成事件而進入 timeout。

## 5. 解決方案

### 5.1 Patch 分析

此問題是透過 **Renesas 提供的 SPI driver patch** 進行修正。

相關 commit：

-   [https://github.com/YDS-Kakip-Team/kakip_linux/commit/814ecf1fd70735d108653f32c79561e0fc41b6c6](https://github.com/YDS-Kakip-Team/kakip_linux/commit/814ecf1fd70735d108653f32c79561e0fc41b6c6)

-   [https://github.com/YDS-Kakip-Team/kakip_linux/commit/77a30cf53385d3d55303511d2de731b89adaf8bb](https://github.com/YDS-Kakip-Team/kakip_linux/commit/77a30cf53385d3d55303511d2de731b89adaf8bb)

這些 commit 對 RSPI driver 進行了較大的修改。

主要改動包括：

#### 5.1.1 SPI communication end detection

Driver 新增對 **SPI communication-end event** 的處理。

這讓 driver 可以正確偵測 SPI transaction 何時真正結束，而不是只依賴 FIFO 狀態。

#### 5.1.2 Interrupt handling 修正

Patch 修正了 SPI interrupt 的設定與處理流程。

這確保：

-   SPI completion event 能正確通知 driver

-   driver 不會等待不存在的 interrupt

#### 5.1.3 FIFO handling 改善

SPI 傳輸流程改為更符合 controller FIFO 行為的設計。

這降低了以下問題的機率：

-   FIFO 狀態錯誤

-   race condition

-   stale status flag

### 5.2 修正後驗證

套用 patch 後再次測試 SPI。

#### 5.2.1 spidev_test

傳輸結果：
```text
TX | ...  
RX | ...
```
SPI 傳輸正常完成，不再出現 timeout。

#### 5.2.2 SPI Device 測試

Pixpaper 電子紙更新正常。

未再觀察到 SPI timeout。

### 5.3 長時間壓力測試

為了驗證 SPI driver 穩定性，進行 **16 小時連續測試**。

| 參數 | 設定 |  
|------------|------------------|  
| SPI device | `/dev/spidev0.0` |  
| SPI speed | 10 MHz |  
| 測試時間 | 16 小時 |

測試結果：

-   未出現 SPI timeout

-   未出現 `SPI transfer failed (-110)`

-   SPI communication 穩定
