
# Broadcom Bluetooth Firmware Bring-up（Kernel 路線）


## 1. 本章定位：為什麼 Kernel 路線才是「量產正解」

在前一章我們已經看到：

-   `brcm_patchram_plus` 能解決「ROM → patched」問題
    
-   但它：
    
    -   需要嚴格控制順序
        
    -   容易與 kernel driver 搶 UART
        
    -   systemd / 開機流程容易 race
        

因此在 **正式 BSP、量產系統、Android / Yocto** 中，  
**主流做法是讓 kernel 全權負責 firmware bring-up**。

本章將完整拆解 **btbcm** 這條路線。

----------

## 2. Kernel Broadcom Bluetooth 架構總覽

### 2.1 相關 driver 與位置
```
drivers/bluetooth/
├─ hci_uart.c        # HCI over UART core
├─ btusb.c           # HCI over USB
├─ btbcm.c           # Broadcom vendor helper
```
### 2.2 分工關係


### 2.2 分工關係

| 元件                     | 負責內容                                     |
|--------------------------|----------------------------------------------|
| hci_uart / btusb         | Transport 層與 HCI device lifecycle 管理     |
| btbcm                    | Broadcom 專屬 firmware 載入與初始化流程      |
| request_firmware         | 從 rootfs 載入 Bluetooth firmware            |
| HCI core                 | Command / Event 處理與狀態機管理              |


**btbcm 不負責 transport**  
它只在「Controller ready 前」插手一次。

----------

## 3. btbcm 的角色與設計理念

### 3.1 btbcm 是「Vendor Helper」

btbcm 的定位非常關鍵：

> **它不是一個完整 driver，而是被 transport driver 呼叫的 helper**

也就是：

-   hci_uart / btusb attach 成功
    
-   kernel 建立 `hci_dev`
    
-   在 power on 或 setup 階段
    
-   **btbcm 被呼叫來完成 firmware download**
    

----------

### 3.2 主要檔案

`drivers/bluetooth/btbcm.c` 

你之後 trace Broadcom kernel bring-up，  
**90% 時間都會在這個檔案**

----------

## 4. Kernel 路線的高階 Bring-up 流程

以下流程以 **UART + serdev + btbcm** 為例：
```
[1] UART driver probe
[2] serdev bluetooth child bind
[3] hci_uart attach
[4] hci_register_dev() → hci0
[5] hci_dev open (power on)
[6] btbcm_initialize()
[7] request_firmware()
[8] download .hcd via HCI vendor commands
[9] controller reset
[10] hci0 ready for mgmt / BlueZ
```
**與 brcm_patchram_plus 最大差異：**

-   全程只有 kernel 一個 master
    
-   不需要 user space 參與 UART
    

----------

## 5. btbcm_initialize()：一切的起點

### 5.1 呼叫時機

btbcm 通常在以下時機被呼叫：

-   hci_dev open
    
-   或 setup callback
    

具體取決於：

-   transport driver
    
-   kernel 版本
    

----------

### 5.2 主要工作內容

`btbcm_initialize()` 做的事：

1.  與 controller 進行基本 HCI handshake
    
2.  讀取 chip id / revision
    
3.  決定 firmware 檔名
    
4.  下載 firmware（`.hcd`）
    
5.  reset controller
    

----------

## 6. 判斷 Chip ID 與 Revision

### 6.1 為什麼要先讀 version？

Broadcom 同一顆型號：

-   不同 revision
    
-   不同 ROM
    
-   可能需要不同 firmware
    

因此 btbcm 會先送：

-   HCI Read Local Version
    
-   Vendor-specific Read Chip ID
    

----------

### 6.2 決定 firmware 名稱

btbcm 內部會根據：

-   chip id
    
-   revision
    
-   transport type（UART / USB）
    
-   有時也考慮 board variant
    

組合出 firmware 檔名，例如：
```
BCM4362A2.hcd
BCM4345C0.hcd
```
**檔名不對 = 100% 失敗**

----------

## 7. request_firmware()：最常見踩雷點

### 7.1 firmware 載入流程

Kernel 呼叫：

`request_firmware(&fw, fw_name, &hdev->dev);` 

系統會：

1.  從 `/lib/firmware/` 尋找檔案
    
2.  若找不到 → 回傳 `-ENOENT`
    
3.  btbcm 初始化中止
    

----------

### 7.2 常見錯誤與症狀


| 現象                              | 原因說明                     |
|-----------------------------------|------------------------------|
| dmesg: firmware not found         | rootfs 未包含 firmware       |
| firmware found but init fail      | firmware 檔案不相容或錯誤    |
| 無任何 btbcm log                  | driver 未執行（未 probe）    |


**Yocto / Android BSP 最常死在這一層**

----------

## 8. `.hcd` 在 Kernel 中如何被下載

### 8.1 與 brcm_patchram_plus 的相同點

-   `.hcd` 仍然是一連串 vendor HCI commands
    
-   仍然是一筆一筆送
    
-   仍然需要等 Command Complete
    

----------

### 8.2 Kernel 下載的優勢

-   UART framing 完全由 kernel 掌控
    
-   不會被 user space 打斷
    
-   transport 設定一致
    

**穩定度遠高於 user space 路線**

----------

## 9. Reset 與 Firmware 生效語意

### 9.1 Reset 是必須的

Firmware 下載完成後：

-   Controller 仍在舊執行環境
    
-   必須 reset 才會跳到 patched state
    

btbcm 會主動送：

`HCI Reset` 

----------

### 9.2 Reset 後的狀態

-   Firmware 已常駐 RAM
    
-   UART baud / power 設定已套用
    
-   等待 HCI core 繼續初始化
    

----------

## 10. Kernel 路線的常見失敗模式

### 10. 1 firmware 找不到

`Direct firmware load  for BCMxxxx.hcd failed with error  -2` 

→ rootfs / recipe / install path 問題

----------

### 10. 2 firmware 不匹配

-   patch download 中途失敗
    
-   reset 後 controller 無回應
    

→ firmware 檔名或 revision 不對

----------

### 10. 3 transport 尚未 ready

-   UART clock / pinmux / power 尚未開
    
-   serdev 綁定不完整
    

→ btbcm 初始化卡在第一個 command

----------

## 11. 為什麼 Kernel 路線更適合 BSP / 量產

### 11.1 穩定性

-   單一控制者（kernel）
    
-   無 tty race
    
-   boot 流程可預期
    

----------

### 11.2 可維護性

-   firmware 由 rootfs 管理
    
-   不依賴 user space tool
    
-   Android / Yocto 官方路線一致
    

----------

## 12. User space vs Kernel 路線總對照


| 項目             | brcm_patchram_plus | btbcm |
|------------------|--------------------|-------|
| 控制權           | User space         | Kernel |
| UART owner       | 容易衝突           | 單一 |
| Boot integration | 複雜               | 乾淨 |
| Debug 難度       | 高                 | 中 |
| 量產適合度       | ✗                 | ✓ |

