
# Bluetooth Stack 全景總覽


## 1. 為什麼 Bluetooth 一定要「分層」理解？

在嵌入式 Linux / Android BSP 上，Bluetooth 問題**極少是單一層的 bug**。  
你實際遇到的會是這種：

-   `hciconfig hci0 up` timeout
    
-   `brcm_patchram_plus` 有時成功、有時失敗
    
-   btmon 看得到 HCI command，但 event 不回來
    
-   firmware 明明有放，卻一直 `request_firmware failed`
    
-   BlueZ 能 power on，卻永遠掃描不到 device
    

這些 **90% 都是「層與層之間的責任邊界沒釐清」**。

所以這一整系列的第一章，只做一件事：  
**把 Bluetooth 從上到下的「層級模型」釘死**。

----------

## 2. Bluetooth Stack 的整體分層（Bird’s-eye View）
```
+--------------------------------------------------+
| User Application                                 |
|  - bluetoothctl                                  |
|  - App (D-Bus client)                             |
+-------------------- D-Bus -----------------------+
| User Space                                       |
|  - bluetoothd (BlueZ daemon)                     |
|  - btmgmt / btmon                                |
+-------------------- mgmt socket -----------------+
| Kernel Space                                     |
|  - Bluetooth Core (HCI / L2CAP / SMP / mgmt)     |
|  - HCI Drivers (btusb / hci_uart)                |
|  - Vendor Helpers (btbcm / btintel / btrtl)      |
+-------------------- Transport -------------------+
| Physical Transport                               |
|  - USB (HCI over USB)                             |
|  - UART (HCI over UART, H4)                      |
+-------------------- HCI -------------------------+
| Bluetooth Controller                             |
|  - ROM firmware                                  |
|  - RAM patch (.hcd)                              |
|  - NVRAM / vendor config                         |
+--------------------------------------------------+

```
----------

## 3. 關鍵角色與「誰負責什麼」

### 3.1 User Space：BlueZ

**BlueZ = policy / state machine 層**

它負責的事：

-   Adapter 管理（power / name / discoverable）
    
-   Device 管理（scan / pair / connect）
    
-   Profile（A2DP / HID / PAN / GATT）
    
-   Key / bonding database
    

它**不負責**的事：

-   ✗ UART / USB 傳輸
    
-   ✗ firmware download
    
-   ✗ baud rate / flow control
    

**BlueZ 從來不直接碰 `/dev/ttyS*` 或 USB endpoint**

----------

### 3.2 mgmt socket：BlueZ 與 Kernel 的「控制面 API」

BlueZ **不是**用 raw HCI socket 控制硬體  
而是用 **mgmt socket**：
```
bluetoothd
   |
   |  (mgmt command)
   v
net/bluetooth/mgmt.c
```
mgmt 負責：

-   power on/off
    
-   start/stop discovery
    
-   set scan parameters
    
-   device state sync
    

關鍵觀念

> **mgmt = control plane（控制面）**  
> **HCI data = data plane（資料面）**

----------

### 3.3 Kernel Bluetooth Core（Host stack）

位置：

`net/bluetooth/` 

核心模組：

-   `hci_core.c` → HCI device lifecycle
    
-   `hci_event.c` → event dispatch
    
-   `mgmt.c` → mgmt control plane
    
-   `l2cap_core.c` → L2CAP data plane
    
-   `smp.c` → pairing / encryption
    

Kernel 的角色是：

> **把「政策」跟「硬體」隔離**  
> User space 決定 _要做什麼_，Kernel 決定 _怎麼跟 controller 講話_

----------

## 4. Transport Layer：USB vs UART 的本質差異

### 4.1 USB（btusb）

-   Driver：`drivers/bluetooth/btusb.c`
    
-   優點：
    
    -   封包邊界明確（USB endpoint）
        
    -   不會有 baud rate 問題
        
    -   CRC / retry 由 USB 處理
        
-   常見用在：
    
    -   PC / x86
        
    -   USB dongle
        
    -   部分 SoC combo module
        

----------

### 4.2 UART（hci_uart）— **嵌入式最常出問題的地方**

-   Driver：`drivers/bluetooth/hci_uart.c`
    
-   Protocol：**HCI H4**
    
-   特性：
    
    -   純 byte stream
        
    -   沒有封包邊界
        
    -   極度依賴 baud rate / RTS/CTS
        

關鍵結論

> **UART Bluetooth 的穩定度 = UART 設定正確度**

----------

## 5. Controller 與 Firmware

Bluetooth controller ≠ dumb device  
它本身是一個「小系統」：

-   ROM firmware（開機即存在）
    
-   RAM patch（下載進去才有完整功能）
    
-   Vendor configuration（NVRAM）
    

### 5.1 Broadcom / Cypress 類型

-   上電後只有 minimal ROM
    
-   必須下載：
    
    -   `.hcd`（patch / minidriver）
        
    -   NVRAM config（晶振 / power / baud / BD_ADDR）
        

這就是為什麼會有：

-   `brcm_patchram_plus`（user space）
    
-   `btbcm`（kernel space）
    

----------

## 6. 兩條「面」一定要分清楚

### 6.1 Control Plane（控制面）

用途：

-   power on/off
    
-   scan
    
-   connect / disconnect
    
-   set parameters
    

路徑：
```
App
 → bluetoothd
   → mgmt socket
 → kernel HCI
       → controller
```
----------

### 6.2 Data Plane（資料面）

用途：

-   ACL data（GATT / A2DP / HID）
    
-   SCO data（語音）
    

路徑：
```
Profile / Socket
 → L2CAP
   → HCI ACL
 → btusb / hci_uart
       → controller
```
**debug 時一定要先判斷你卡在哪一條 plane**

----------

## 7. 一個「正確的 debug 心智模型」

當 Bluetooth 壞掉時，你要問的不是  
✗「為什麼藍牙不能用？」

而是依序問：

1.  **Kernel HCI device 有沒有起來？**
    
    -   `hciconfig -a`
        
    -   `btmgmt info`
        
2.  **HCI command/event 有沒有正常往返？**
    
    -   `btmon`
        
3.  **Transport 是否穩定？**
    
    -   USB：`dmesg | grep btusb`
        
    -   UART：baud / RTS / CTS / tty ownership
        
4.  **Firmware 是否正確載入？**
    
    -   `dmesg | grep firmware`
        
    -   vendor driver log
        
5.  **最後才是 BlueZ / profile / policy**
