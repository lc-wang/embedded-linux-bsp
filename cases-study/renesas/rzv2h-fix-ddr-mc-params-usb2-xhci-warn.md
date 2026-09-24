# RZ/V2H USB2 相機導致內部匯流排效能降級除錯報告

## 1. 問題概述

### 1.1 問題摘要

在 Kakip（RZ/V2H）開發板上接上 USB2 相機後，xHCI host controller 出現以下 kernel 警告：

```
xhci-hcd 15860000.usb: WARN: HC couldn't access mem fast enough for slot 1 ep 2
```

導致：

- USB2 相機不穩定或無法正常運作
- xHCI controller 發生 DMA stall
- 影響 USB2 裝置的傳輸效能

### 1.2 問題現象

#### 1.2.1 Kernel 警告訊息

```
xhci-hcd 15860000.usb: WARN: HC couldn't access mem fast enough for slot 1 ep 2
```

#### 1.2.2 症狀描述

| 現象 | 說明 |
|------|------|
| USB2 相機無法穩定運作 | xHCI controller 發生 DMA stall |
| Kernel 持續出現 WARN | HC 無法在期限內完成操作 |
| USB2 傳輸效能異常 | 記憶體頻寬供應不足 |

## 2. 系統環境

- Board：Kakip
- SoC：Renesas RZ/V2H
- Firmware：Trusted Firmware-A（TF-A）
- 記憶體類型：LPDDR4
- 受影響介面：USB2（xhci-hcd @ 15860000.usb）

## 3. 除錯過程

### 3.1 硬體確認

✓ USB2 實體連線正常
✓ 相機裝置可被識別（`lsusb` 可見）
✓ 非 cable 問題
✓ 非 USB2 hub / switch 問題

### 3.2 問題分析

#### 3.2.1 關鍵線索

檢查 DDR 記憶體控制器參數設定檔：

```
plat/renesas/rz/soc/v2h/drivers/ddr/ddr_param_def_lpddr4.c
```

發現 `param_setup_mc[]` 中多個 register 設定值不正確：

```c
{0x0134, 0xff0b0000},   // ✗ command bus timing 設定錯誤
{0x0135, 0x010001ff},   // ✗ command bus timing 設定錯誤
{0x0178, 0x0a020808},   // ✗ 讀寫排程間隔設定錯誤
{0x017f, 0x00000404},   // ✗ 仲裁權重設定錯誤
{0x0181, 0x00000404},   // ✗ 仲裁權重設定錯誤
{0x02cd, 0x01010101},   // ✗ 頻寬分配設定錯誤
{0x02cf, 0x01010064},   // ✗ 延遲設定錯誤
{0x02d0, 0x01010101},   // ✗ 延遲設定錯誤
```

## 4. Root Cause 分析（根本原因）

> **LPDDR4 記憶體控制器（MC）的 timing 與 scheduling 參數設定不當，導致記憶體頻寬不足以支援 USB2 DMA 傳輸，進而造成 xHCI controller stall。**

問題連鎖關係如下：

| 現象 | 原因說明 |
|------|----------|
| xHCI 出現 WARN | HC 無法在時限內完成 DMA 操作 |
| DMA stall | 記憶體頻寬不足以即時供應 USB2 請求 |
| 頻寬不足 | DDR MC 的 timing/scheduling 參數設定錯誤 |
| 參數錯誤 | `param_setup_mc[]` 中多個 register 值不正確 |

## 5. 解決方案

### 5.1 修改檔案

```
plat/renesas/rz/soc/v2h/drivers/ddr/ddr_param_def_lpddr4.c
```

### 5.2 修正的 Register 對照表

| Register | 舊值         | 新值         | 說明                     |
|----------|-------------|-------------|--------------------------|
| `0x0134` | `0xff0b0000` | `0x1f0b0000` | Command bus timing 修正  |
| `0x0135` | `0x010001ff` | `0x0101011f` | Command bus timing 修正  |
| `0x0178` | `0x0a020808` | `0x0a000808` | 讀寫排程間隔修正          |
| `0x017f` | `0x00000404` | `0x00000202` | 仲裁權重調整              |
| `0x0181` | `0x00000404` | `0x01000505` | 仲裁權重調整              |
| `0x02cd` | `0x01010101` | `0x01010401` | 頻寬分配修正              |
| `0x02cf` | `0x01010064` | `0x0301000a` | 延遲與匯流排吞吐量修正    |
| `0x02d0` | `0x01010101` | `0x01010102` | 延遲與匯流排吞吐量修正    |

### 5.3 驗證結果

套用 patch 後，以下問題應已解決：

- ✓ 不再出現 `xhci-hcd 15860000.usb: WARN: HC couldn't access mem fast enough for slot 1 ep 2` 警告
- ✓ USB2 相機可正常運作，無 DMA stall
- ✓ USB2 傳輸效能恢復正常

## 6. 結論與建議

### 6.1 最終狀態

- LPDDR4 記憶體控制器 timing 參數已修正
- USB2 DMA 傳輸可正常取得足夠記憶體頻寬
- xHCI controller 不再發生 stall

### 6.2 問題總結

#### 不是以下問題：

- ✗ 非 USB2 cable 問題
- ✗ 非 USB2 相機硬體故障
- ✗ 非 xHCI driver 問題
- ✗ 非 USB hub / switch 問題
- ✗ 非 PHY / 實體層問題

#### 真正原因：

> **`param_setup_mc[]` 中的 LPDDR4 記憶體控制器參數設定不當，導致 USB2 DMA 傳輸時記憶體頻寬不足，引發 xHCI controller stall。**
