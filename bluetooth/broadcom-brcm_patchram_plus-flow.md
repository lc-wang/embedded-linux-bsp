# Broadcom Bluetooth Firmware Bring-up（User Space 路線）

在 Broadcom / Cypress / Infineon UART Bluetooth 平台上，  
`brcm_patchram_plus` 幾乎是 bring-up 初期一定會用到的工具。

但實務上你一定遇過這種狀況：

-   `brcm_patchram_plus` 看起來成功結束
    
-   firmware `.hcd` 也確實有送
    
-   但：
    
    -   `hciconfig hci0 up` timeout
        
    -   btmon 看不到後續 event
        
    -   掃描不到任何 device
        
**原因在於：brcm_patchram_plus 只是「初始化的一部分」**  
它不是 Bluetooth stack，也不保證 kernel 接手後一定成功。

本章會把它當成一個「狀態轉換器」來看，而不是魔法工具。

## 1. brcm_patchram_plus 在整個 Stack 中的位置

### 1.1 正確的心智模型
```
(brcm_patchram_plus)
User space
  └─ 初始化 Controller（ROM → patched state）
       │
       ▼
Kernel (hci_uart / btusb)
  └─ 建立 hci_dev
       │
       ▼
BlueZ
```
**brcm_patchram_plus 的責任只到「Controller ready」為止**  
後面的：

-   HCI device lifecycle
    
-   mgmt control
    
-   scan / connect
    
全部是 kernel + BlueZ 的事。

## 2. brcm_patchram_plus 的高階流程總覽

以 UART Broadcom controller 為例，實際流程可拆成 6 個階段：
```
[1] Open UART device
[2] Basic HCI handshake (ROM mode)
[3] (Optional) Enter minidriver
[4] Download .hcd firmware patch
[5] Configure runtime parameters (baud, sleep, etc.)
[6] Reset & exit, hand over to host stack
```
每一階段都對應 **特定的 vendor HCI command sequence**。

## 3. 階段 1：UART 開啟與 Host 端設定

### 3.1 brcm_patchram_plus 做的第一件事

-   `open("/dev/ttySx")`
    
-   設定：
    
    -   raw mode
        
    -   baud rate（通常先 115200）
        
    -   flow control（依參數）
        
**這一步如果跟 kernel driver 同時做，後面一定爆**

### 3.2 常見第一層錯誤

-   UART node 不對（ttySx 選錯）
    
-   UART clock / pinmux 尚未 ready
    
-   flow control 與硬體不符
    
症狀：

-   工具一開始就卡住
    
-   或第一個 HCI Reset 沒回 event
    
## 4. 階段 2：ROM 模式下的基本 HCI Handshake

### 4.1 第一個一定會送的 command

`HCI Reset  (0x0C03)` 

目的：

-   確認 controller 活著
    
-   確認 UART 對話正常
    
btmon 觀察點：
```
< HCI Command: Reset
> HCI Event: Command Complete (Reset)
```
**如果這一步沒過，後面完全不用看**

### 4.2 Read Local Version（判斷 chip / revision）

接下來通常會送：

`HCI Read  Local  Version Information` 

用途：

-   判斷是哪一顆 Broadcom chip
    
-   決定後續 patch 流程（或只是 log）
    
## 5. 階段 3：Minidriver

### 5.1 Minidriver 是什麼？

某些 Broadcom chip 需要先下載一個 **小型暫時性 firmware**：

-   用途：
    
    -   提供 RAM download 能力
        
    -   修正 ROM bug
        
-   特性：
    
    -   非最終 firmware
        
    -   通常下載後馬上 reset
        
**不是每顆 chip 都需要**

### 5.2 Minidriver 相關錯誤

-   minidriver 與 chip revision 不符
    
-   ROM 不接受該 command
    
症狀：

-   event status 非 0
    
-   後續 firmware download 全失敗
    
## 6. 階段 4：`.hcd` Firmware Patch 下載流程

### 6.1 `.hcd` 的本質

`.hcd` **不是 binary image**  
而是：

> **一連串 vendor-specific HCI commands 的集合**

每一筆都大概是：

`Vendor Opcode + RAM write parameters` 

### 6.2 實際下載行為

brcm_patchram_plus 會：

1.  讀取 `.hcd`
    
2.  切成多個 HCI vendor command
    
3.  一筆一筆送下去
    
4.  等待每筆 command 的 completion
    
**這是一個高度同步、對 framing 極度敏感的流程**

### 6.3 btmon 中你會看到什麼
```
< HCI Command: Broadcom Write RAM
> HCI Event: Command Complete
(repeat many times)
```
如果中途卡住：

-   幾乎一定是 UART framing / baud / flow control
    
## 7. 階段 5：Runtime 參數設定

### 7.1 常見設定項目

-   切換最終 baud rate（例如 3M）
    
-   設定 sleep mode
    
-   啟用/停用某些 power feature
    
這些設定 **不是 firmware patch 的一部分**，  
而是工具在 patch 後另外送的 vendor command。

### 7.2 最經典錯誤：下載時就切高 baud

`--use_baudrate_for_download` 

風險：

-   firmware download packet 很密
    
-   高 baud + framing error = 災難
    
實務建議：

-   download：115200
    
-   runtime：再切高 baud
    
## 8. 階段 6：Reset & 交棒給 Host Stack

### 8.1 最後一定會做的事

-   HCI Reset
    
-   關閉 UART（brcm_patchram_plus exit）
    
此時 controller 狀態是：

-   firmware 已載
    
-   baud 已切
    
-   等待 host stack attach
    
### 8.2 常見交棒失敗情境

-   kernel driver 已經先 attach（搶 tty）
    
-   kernel 用的 baud / flow 與工具不同
    
-   controller reset 後回到 ROM（patch 沒生效）
    
症狀：

-   hci0 出現但 up 不起來
    
-   或 hci0 根本沒出現
    
## 9. User Space 路線的「使用守則」

**只能選一種控制權模型：**

-   ✓ brcm_patchram_plus → kernel attach（嚴格控順序）
    
-   ✓ kernel btbcm 全權處理
    
-   ✗ 兩邊混用
    
**混用 = 長期不穩定**

## 10. 常見問題與排查（brcm_patchram_plus 的典型失敗模式對照表）

| 症狀                     | 高機率原因                         |
|--------------------------|------------------------------------|
| Reset 無任何 Event       | UART / Baud rate / Pinmux 問題     |
| 中途卡在 Patch Download  | Framing error / Flow control 錯誤 |
| Patch 完成但模組不可用   | Firmware 交棒失敗                  |
| 偶爾成功                 | TTY 被多方同時使用                 |
| 更換 Firmware 無效       | 根因在 UART 介面                   |
