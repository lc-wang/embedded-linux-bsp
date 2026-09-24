# Bluetooth Source Tree 導覽

在實際 debug Bluetooth 問題時，最常見的困境不是「看不懂 code」，而是：

> **「我根本不知道現在這個問題，該從哪個檔案開始看」**

例如：

-   `hciconfig hci0 up` timeout
    
-   btmon 看到 command 但 event 沒回來
    
-   firmware 明明放了，卻沒載
    
-   brcm_patchram_plus 跟 kernel driver 行為打架
    
這一章的目的只有一個：

> **建立一張「可操作的 source tree mental map」**  
> 讓你知道「某一類問題，該直接進哪個目錄、哪個檔案」。

## 1. 整體 Source Tree 分佈總覽
```
User Space
├─ bluez/                         (bluetoothd, tools)
│
Kernel Space
├─ net/bluetooth/                 (Bluetooth core / protocols)
├─ drivers/bluetooth/             (HCI transport & vendor drivers)
│
Transport / TTY / USB
├─ drivers/tty/                   (UART / serdev)
├─ drivers/usb/                   (USB core)
```
**Bluetooth 不只在 `drivers/bluetooth/`**  
很多關鍵邏輯其實在 `net/bluetooth/`。

## 2. User Space：BlueZ Source Tree

### 2.1 BlueZ 原始碼根目錄結構（重點）
```
bluez/
├─ src/
│  ├─ main.c              # bluetoothd entry point
│  ├─ adapter.c           # Adapter object (hci0)
│  ├─ device.c            # Remote device state machine
│  ├─ mgmt.c              # mgmt socket interface
│  ├─ profile.c           # profile framework
│
├─ profiles/
│  ├─ audio/              # A2DP / AVRCP
│  ├─ input/              # HID
│  ├─ network/            # PAN
│  ├─ gatt/               # GATT server/client
│
├─ tools/
│  ├─ bluetoothctl.c      # CLI over D-Bus
│  ├─ btmgmt.c            # mgmt socket tool (no bluetoothd)
│  ├─ btmon.c             # HCI traffic monitor
```

### 2.2 bluetoothd 的「核心控制點」

#### Entry point

`src/main.c` 

你可以從這裡看到：

-   D-Bus 初始化
    
-   mgmt socket 初始化
    
-   Adapter manager 啟動流程
    
#### Adapter（hci0）的核心狀態機

`src/adapter.c` 

負責：

-   Powered / Discoverable / Pairable
    
-   StartDiscovery / StopDiscovery
    
-   與 kernel mgmt 的狀態同步
    
如果問題是「BlueZ 顯示 power on，但實際硬體沒反應」，  
**一定要看這裡 + kernel mgmt**。

#### mgmt socket（BlueZ ↔ Kernel）

`src/mgmt.c` 

這個檔案是 **BlueZ 與 kernel Bluetooth core 的唯一控制通道**。

重要觀念：

-   BlueZ **不送 raw HCI command**
    
-   BlueZ 送的是 **mgmt command**
    
-   kernel 再轉成 HCI command
    
## 3. Kernel Space：Bluetooth Core（Host Stack）

### 3.1 `net/bluetooth/` 是整個核心
```
net/bluetooth/
├─ hci_core.c        # HCI device lifecycle, command queue
├─ hci_event.c       # HCI event parsing / dispatch
├─ hci_sock.c        # Raw HCI socket (btmon)
├─ mgmt.c            # mgmt control plane
├─ l2cap_core.c      # L2CAP data path
├─ smp.c             # pairing / encryption
```
**Debug 原則**

-   「控制失敗」→ `mgmt.c` / `hci_core.c`
    
-   「command timeout」→ `hci_core.c` / `hci_event.c`
    
-   「資料傳不動」→ `l2cap_core.c` / driver
    
### 3.2 HCI device 是怎麼出現的？

關鍵 API：

`hci_register_dev(struct hci_dev *hdev);` 

這個呼叫通常發生在：

-   btusb probe 成功後
    
-   hci_uart attach 成功後
    
如果你 **根本看不到 hci0**  
→ 問題一定在 driver 層，還沒進到 BlueZ。

## 4. Kernel Space：Bluetooth Drivers

### 4.1 drivers/bluetooth/ 目錄總覽
```
drivers/bluetooth/
├─ btusb.c           # HCI over USB
├─ hci_uart.c        # HCI over UART (core)
├─ hci_ldisc.c       # TTY line discipline (N_HCI)
├─ btbcm.c           # Broadcom vendor helper
├─ btintel.c         # Intel firmware helper
├─ btrtl.c           # Realtek firmware helper
```

### 4.2 btusb（USB 藍牙）

`drivers/bluetooth/btusb.c` 

負責：

-   USB endpoint 設定
    
-   URB submit / complete
    
-   HCI command / event / ACL data 傳輸
    
如果是 USB dongle：

-   問題多半在這個檔案 + USB core
    
-   幾乎不會碰到 baud / framing 類問題
    
### 4.3 hci_uart（UART 藍牙核心）

`drivers/bluetooth/hci_uart.c` 

負責：

-   HCI device 與 UART transport 的 glue
    
-   protocol abstraction（H4, BCSP, …）
    
-   attach / detach lifecycle
    
它本身 **不直接解析 byte framing**，  
而是配合：

-   line discipline（舊式）
    
-   或 serdev（新式 DT）
    
### 4.4 hci_ldisc（TTY line discipline）

`drivers/bluetooth/hci_ldisc.c` 

用途：

-   把某個 `/dev/ttySx` 掛成 N_HCI
    
-   接管該 tty 的 read/write
    
-   將 byte stream 丟給 hci_uart
    
**這就是為什麼 brcm_patchram_plus 容易跟 kernel 打架**  
因為兩邊都想「擁有 tty」。

### 4.5 btbcm（Broadcom firmware helper）

`drivers/bluetooth/btbcm.c` 

負責：

-   讀 Broadcom chip id / revision
    
-   決定 firmware 檔名
    
-   `request_firmware()`
    
-   將 `.hcd` 拆成 vendor HCI commands 下載
    
如果你走「kernel 自動載 firmware」方案  
→ **90% 時間都會在這個檔案打轉**

## 5. Transport 關聯：TTY / serdev / USB

### 5.1 UART / TTY / serdev
```
drivers/tty/
├─ serial/           # UART controller drivers
├─ serdev/           # serdev framework
```
-   傳統：`hciattach` + `hci_ldisc`
    
-   新式（DT）：serdev child device → hci_uart
    
DT / power / clock / reset 問題  
**不會出現在 Bluetooth driver 裡，而是在 UART driver / DT**

### 5.2 USB Core

`drivers/usb/` 

btusb 只是 client：

-   真正的 error 可能來自 USB core / PHY / power
    
## 6. 常見問題與排查（問題導向：你現在該看哪？）

### 6.1 問題 1：看不到 hci0
```
drivers/bluetooth/btusb.c
drivers/bluetooth/hci_uart.c
```

### 6.2 問題 2：hci0 存在，但 up 不起來
```
net/bluetooth/hci_core.c
net/bluetooth/hci_event.c
btmon（搭配）
```

### 6.3 問題 3：firmware 沒載 / 載錯
```
drivers/bluetooth/btbcm.c
request_firmware()
/lib/firmware/*
```

### 6.4 問題 4：BlueZ 顯示異常
```
bluez/src/adapter.c
bluez/src/mgmt.c
net/bluetooth/mgmt.c
```
