# Bluetooth Transport 架構對照

在實務討論 Bluetooth bring-up 時，常會聽到一句話：

> 「UART 太不穩了，改用 USB 吧。」

這句話 **一半正確，一半危險**。

-   ✓ USB Bluetooth 的確比較「好起來」
    
-   ✗ 但它不是「免費解法」，而是 **系統架構選擇**
    
本章會從 **kernel / transport / firmware / debug** 四個角度，  
系統性對照 **btusb vs hci_uart**，讓你在 BSP / board 設計階段就做對決策。

## 1. Transport 的本質差異

### 1.1 USB：封包導向（Packet-oriented）

USB 的特性：

-   有 **endpoint**
    
-   有 **transaction 邊界**
    
-   有 **CRC / retry**
    
-   Host controller 管理 flow control
    
Bluetooth over USB 幾乎不需要關心 framing

### 1.2 UART：純 byte stream（Stream-oriented）

UART 的特性：

-   無封包邊界
    
-   無 CRC
    
-   無 retry
    
-   所有 framing / flow control 由軟體處理
    
Bluetooth over UART **任何 byte 錯誤都會擴散成災難**

## 2. Kernel Driver 架構對照

### 2.1 USB Bluetooth（btusb）

主要檔案：

`drivers/bluetooth/btusb.c` 

架構特點：

-   標準 USB driver model
    
-   probe → 設定 endpoints → 註冊 hci_dev
    
-   HCI command/event 封裝在 USB URB 中
    
資料流（簡化）：
```
HCI core
  ↕
btusb
  ↕
USB core
  ↕
USB Host Controller
```

### 2.2 UART Bluetooth（hci_uart）

主要檔案：
```
drivers/bluetooth/hci_uart.c
drivers/bluetooth/hci_ldisc.c
```
架構特點：

-   依賴 tty / serdev
    
-   H4 framing 完全在軟體
    
-   transport 與 power / clock 強烈耦合
    
資料流（簡化）：
```
HCI core
  ↕
hci_uart
  ↕ tty / serdev
  ↕
UART controller
```

## 3. Firmware Bring-up 的差異

### 3.1 USB Bluetooth 的 firmware 世界

-   多數 USB BT controller：
    
    -   firmware 由 kernel 自動處理
        
    -   不需要 user space 工具
        
-   firmware download：
    
    -   仍透過 HCI vendor commands
        
    -   但 transport 穩定度高
        
常見感受：

> 「USB BT 幾乎不會卡在 firmware 階段」

### 3.2 UART Bluetooth 的 firmware 世界

-   幾乎一定需要：
    
    -   brcm_patchram_plus（user space）
        
    -   或 btbcm（kernel）
        
-   firmware download 對：
    
    -   baud
        
    -   RTS/CTS
        
    -   framing  
        極度敏感
        
常見感受：

> 「同一套 firmware，在 USB 好好的，在 UART 就炸」

## 4. 穩定度比較

### 4.1 為什麼 USB 穩定？

-   framing 由硬體處理
    
-   CRC 自動校驗
    
-   retry 由 USB host controller 負責
    
-   不受系統 load 影響
    
**USB BT 的穩定度幾乎與 CPU load 無關**

### 4.2 為什麼 UART 不穩？

-   framing 全靠軟體
    
-   高 baud rate 時容易掉 byte
    
-   RTS/CTS 稍有不一致就出事
    
-   系統 load / interrupt latency 會影響接收
    
**UART BT 對「系統品質」非常敏感**

## 5. Debug 成本差異

### 5.1 USB Bluetooth Debug 成本

常見 debug 路徑：

-   `dmesg | grep btusb`
    
-   `lsusb -t`
    
-   `btmon`
    
問題分類清楚：

-   USB enumeration 問題
    
-   firmware missing
    
-   HCI protocol 問題
    
**問題通常集中在一層**

### 5.2 UART Bluetooth Debug 成本

常見 debug 路徑：

-   `stty -F /dev/ttySx -a`
    
-   `lsof /dev/ttySx`
    
-   `btmon`
    
-   scope / logic analyzer
    
問題交錯：

-   UART driver
    
-   pinmux / clock
    
-   firmware
    
-   user space tool
    
-   kernel race
    
**debug 成本是 USB 的數倍**

## 6. 系統設計選型建議

### 6.1 什麼情境適合 USB Bluetooth

-   有 USB host controller
    
-   board space / BOM 可接受
    
-   追求穩定度 > 省腳位
    
-   量產 / 商用產品
    
**首選 USB**

### 6.2 什麼情境不得不用 UART Bluetooth

-   SoC 無多餘 USB
    
-   combo chip 強制 UART
    
-   超低功耗需求
    
-   BOM 極度受限
    
**選 UART 但要付出整合成本**

## 7. 快速判斷問題層級的「分流技巧」

當你同時有 USB 與 UART 版本的 module：

1.  USB 版本正常？
    
    -   是 → 問題幾乎一定在 UART transport
        
    -   否 → firmware / controller 問題
        
2.  同 firmware 在 USB OK、UART 不 OK？
    
    -   100% 不是 firmware bug
        
**USB 是非常好的「對照組」**

## 8. Android / Yocto 實務觀察

-   Android reference design：
    
    -   越來越偏 USB / PCIe
        
-   Yocto BSP：
    
    -   UART BT 仍大量存在
        
    -   但 kernel 路線（btbcm + serdev）成為主流
        
趨勢總結：

> 「UART BT 不會消失，但門檻越來越高」
