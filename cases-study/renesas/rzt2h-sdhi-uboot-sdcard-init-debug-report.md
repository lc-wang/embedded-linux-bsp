
# **RZ/T2HSD 卡初始化失敗技術分析報告**

**Issue：U-Boot SD card init timeout (`-110: Card did not respond to voltage select`)**

----------

# 1. 背景說明

在 RZ/T2H（R9A09G077M）平台上使用 U-Boot 2021.10（Renesas BSP 分支）進行 SD 卡啟動時，觀察到：
```bash
selecting mode MMC legacy (freq : 0 MHz)
Card did not respond to voltage select! : -110
```
此錯誤會造成：

-   U-Boot 無法掛載 SD 卡
-   無法載入環境變數
-   無法載入 kernel image
-   無法啟動系統
    

本問題表面上看似：

-   DTS pinctrl / 電壓
-   SDHI clock
-   OCR / HCS 問題
-   SD 卡容量差異（16GB vs 32GB）
    

但實際根因 **與上述全部無關**。

----------


# 2. 現象與可重現流程

## 2.1 不同容量 SD 卡的差異行為

| 容量 | 卡種 | 行為 |
|------|------|------|
| 32GB | SDHC/SDXC | 可正常啟動 |
| 16GB | SDHC（常見舊版本） | 一定失敗，顯示 -110 |

> 注意：  
> 這一點在 Debug 初期造成誤導，但後續調查證明這只是 **副作用**，非真正 root cause。

----------

## 2.2 mmc 指令測試結果

在 U-Boot shell 中：
```bash
=> mmc list
mmc@92080000: 0
mmc@92090000: 1

=> mmc dev 0
selecting mode MMC legacy (freq : 0 MHz)
Card did not respond to voltage select! : -110

=> mmc dev 1
selecting mode MMC legacy (freq : 0 MHz)
Card did not respond to voltage select! : -110

```
兩個控制器都顯示 **freq = 0 MHz** 並 timeout。

----------

# 3. Debug 流程

本問題的 Debug 非常不直覺，歷經下列階段。

----------

## 3.1 DTS 層面驗證

### 檢查內容：

-   `sdhi0` / `sdhi1` node    
-   clock 設定是否正確
-   regulator 是否連上（`vmmc-supply` / `vqmmc-supply`）
-   alias (`mmc0`, `mmc1`)
-   pinctrl 是否存在

### 結果：

-   DTS 實際內容是 **正確、有定義、有 clocks、有 interrupt**
-   Board DTS 也有開啟 `sdhi0` / `sdhi1` (`status = "okay"`)
-   regulator 也能正常提供 3.3V

→ **DTS 並不是造成 -110 的根因**

----------

## 3.2 mmc OCR、HCS 測試

我們曾修改這行：
```c
mmc->ocr &= ~(OCR_HCS | OCR_S18R);
```
清掉 HCS/S18R 原本會造成：

-   SDSC 舊卡 fail
-   SDHC 卡正常
    

但在刪除這行後，**問題依然存在** → 排除 OCR 根因。

----------

## 3.3 硬體端（pinmux/power）測試

加入：
```dts
vmmc-supply = <&reg_3p3v>;
vqmmc-supply = <&reg_3p3v>; 
```
仍然：

-   clock = 0 MHz  
-   卡完全無回應  

排除 pinmux/電源根因。

----------

# 4. **真正的 Root Cause：錯誤的 SDHI power-cycle 程序（U-Boot 私增 patch）**

最終根因確認於：

`drivers/mmc/sh_sdhi.c` 

該檔案中的一段 Renesas 特有 code：
```c
#if ((defined CONFIG_R9A09G077) || (defined CONFIG_R9A09G087))
sh_sdhi_writel(host, SDHI_SD_STATUS,
               ~SD_STATUS_SD_PWEN & sh_sdhi_readl(host, SDHI_SD_STATUS));
mdelay(6);
sh_sdhi_writel(host, SDHI_SD_STATUS, SD_STATUS_SD_PWEN);
#endif
```
這段 code 來自以下 commit：
```yaml
ed302f38a8e28604fc13e9af5e8fd9eecc3101a6  "mmc: sh_sdhi: Fix fail to boot sd card"
```
該 commit 強制加入 **SD 卡硬體電源循環 (power-cycle)**：

1.  SD_PWEN OFF
2.  mdelay(6)
3.  SD_PWEN ON
    

----------

## 4.1 為什麼這會造成 -110？

### ✓ 16GB 疑似「舊版 SDHC」不接受這種強制 power-cycle

會導致其：

-   上電初始化序列被打斷 
-   state machine 進入不一致狀態
-   無法回應 ACMD41
    

### ✓ U-Boot 在 power-cycle **之後**立刻發 CMD1/ACMD41

對於部分卡：

-   電氣層尚未穩定
-   內部 reset 未完成

結果必然是：
```bash
Card did not respond to voltage select : -110
```
### ✓ 更改 mdelay() 6 → 20ms 無效

證明卡本身對此強制 power-cycle 不兼容，而不是單純 timing 不足。

----------

# 5. 解法：Revert 整個 commit

解法：
```bash
git revert ed302f38a8e28604fc13e9af5e8fd9eecc3101a6 
```
並且也 revert 掉 SDHI_SD_STATUS register 操作。

### revert 後：

-   16GB SDHC 正常讀取
-   32GB 正常
-   所有容量都可正常 boot
-   `mmc dev 0` / `mmc dev 1` 均正常進入 25MHz legacy mode
----------

# 6. 實際修復 commit（摘要）
```bash
Revert "mmc: sh_sdhi: Fix fail to boot sd card"

The reverted commit introduces a mandatory hardware power-cycle  sequence  for SD card initialization specific to Renesas RZ/T2H.

This power-cycle corrupts card initialization on common SDHC 16GB cards,
causing ACMD41 timeout (-110). Reverting restores correct behavior.
```
Revert 部分包含：

-   移除 SDHI_SD_STATUS 暫存器定義    
-   移除 `SD_STATUS_SD_PWEN` bit 操作
-   移除強制 power-cycle
    
----------

# 7. 最終驗證（成功）
```bash
`=> mmc dev 0
mmc0: SDHC, 25 MHz
=> mmc info
Device: mmc@92080000
...
```

啟動 log：
```bash
reading  Image  reading  uInitrd  reading  boot.scr  Booting  Linux... 
```
系統正常啟動。

