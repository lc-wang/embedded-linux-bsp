# Kakip Image 燒錄問題技術分析報告

## 1. 問題概述

### 1.1 背景

Kakip OS 提供完整的 SD card 映像檔，需要以 dd 寫入 SD card 作為啟動介質。

正常流程為：

1.  清除舊 partition table（選擇性）
    
2.  使用 `dd` 寫入 SD card
    
3.  將 SD card 插入 Kakip 板並啟動
    

實際測試中發現以下異常：

-   使用一般 dd 寫法（無 `oflag=direct`）燒錄後無法開機
    
-   加上 `oflag=direct` 後燒錄可正常開機
    

本文件分析原因並提供最佳實務建議。

----------

### 1.2 問題描述

以下指令燒錄後無法從 SD card 開機：

`sudo dd  if=kakip_os_image_v7.4.img of=/dev/sde bs=4M status=progress` 

但加入 Direct I/O 後可正常開機：

`sudo dd  if=kakip_os_image_v7.4.img of=/dev/sde bs=4M status=progress oflag=direct` 

----------

## 2. 分析過程（技術分析）

### 2.1 dd 預設使用 Linux page cache

一般 dd 寫入流程為：

1.  資料寫入 page cache
2.  Linux 標示為已寫入
3.  實際裝置寫入延後進行，順序不可控
4.  `sync` 後才強制 flush
    

此行為對 boot sector 造成風險：

-   寫入順序可能錯亂
-   未對齊寫入會破壞 SPL 或 GPT header
-   cache 未即時 flush 時，前 1MB 可能為舊資料或部分未寫入

----------

### 2.2 Direct I/O（oflag=direct）避免對齊與 flush 問題

Direct I/O 特性：

-   不使用 cache
-   寫入順序維持一致
-   寫入立即落盤
-   block 大小與 offset 直接與底層裝置對齊
    

因此：

-   MBR/GPT header
-   SPL
-   U-Boot image header
    

都能被完整寫入正確位置。

----------

### 2.3 讀卡機差異造成的不一致性

不同 USB SD 讀卡器的 firmware 流程差異很大，有些會：

-   做 4K cache
-   使用非同步寫入
-   延遲 flush
-   做內部 sector re-map
    

因此，有些環境即使沒有 direct I/O 也可正常啟動，但部分裝置必須使用 direct I/O 才能保證寫入正確。

----------

### 2.4 實測與驗證

可以比對 SD 卡與原始映像檔的前 1MB：

`sudo hexdump -C /dev/sde | head hexdump -C kakip_os_image_v7.4.img | head` 

若未使用 `oflag=direct`，可能看到：
-   開頭 sector 不一致
-   前幾個 block 若為 `00 00`，表示未寫入成功
-   SPL header 損壞
    

----------

## 3. Root Cause 分析（問題根因摘要）

不加 `oflag=direct` 時，Linux 會使用 page cache 寫入 SD card，可能導致：

-   開機區域（MBR、GPT header、SPL）未即時寫入
-   block 寫入順序被重新排序
-   對齊不正確
-   寫入延遲造成前幾個 sectors 的資料不完整
    

Kakip（RZ/V2H）啟動流程強依賴 SD 開頭區段：

-   sector 0（MBR/GPT）
-   SPL 在 SD card 前段
-   U-Boot 與 FIT 在固定 offset
    

任意小塊損壞都會導致無法啟動。

加入 `oflag=direct` 可以避免上述問題，確保所有 block 以對齊方式直接寫入裝置。

----------

## 4. 解決方案

### 4.1 最佳化燒錄指令

以下為最穩定建議：

`sudo dd  if=kakip_os_image_v7.4.img of=/dev/sde bs=4M status=progress oflag=direct,sync  sync` 

說明：

-   `oflag=direct` ：避免 cache 和 re-ordering
-   `oflag=sync` ：每個 block 寫入後立即同步到 SD
-   `bs=4M` ：效能與對齊兼具
    

----------

### 4.2 建議的完整燒錄流程

#### 步驟一：清除舊 GPT/MBR（避免分割表殘留）

`sudo sgdisk --zap-all /dev/sde` 

#### 步驟二：燒錄映像檔

`sudo dd  if=kakip_os_image_v7.4.img of=/dev/sde bs=4M status=progress oflag=direct,sync` 

#### 步驟三：強制完成寫入

`sync` 

#### 步驟四：重新插拔 SD 卡

----------

## 5. 結論與建議

Kakip v7.4 映像檔在燒錄 SD card 時，若未使用 `oflag=direct`，Linux page cache 有機會造成：
-   部分 boot sector 未寫入
-   SPL 或 GPT header 損壞
-   導致板子無法啟動
    

加入 `oflag=direct,sync` 可確保：
-   每個 block 寫入裝置
-   排除 cache 與對齊問題
-   保證 boot sector 正確保存
