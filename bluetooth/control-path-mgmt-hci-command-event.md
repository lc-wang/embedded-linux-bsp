# Bluetooth Control Path 深入解析

在 Bluetooth bring-up / debug 時，最常見、也最難解的問題幾乎都集中在 **control path**：

-   `hciconfig hci0 up` 卡住
    
-   `btmgmt power on` 沒反應
    
-   btmon 看到 command，但 event 沒回來
    
-   BlueZ 顯示 power on，但 controller 實際沒動作
    
這一章只做一件事：  
**把「mgmt → HCI command → HCI event → completion」這條路完整拆解**

## 1. Control Path 全景
```
User space
──────────
bluetoothd / btmgmt
        │
        │  (mgmt command)
        ▼
Kernel space
────────────
net/bluetooth/mgmt.c
        │
        │  (translate)
        ▼
net/bluetooth/hci_core.c
        │
        │  (HCI Command packet)
        ▼
HCI Driver (btusb / hci_uart)
        │
        │  (transport)
        ▼
Bluetooth Controller
        │
        │  (HCI Event)
        ▲
        │
net/bluetooth/hci_event.c
        │
        │  (complete / notify)
        ▼
mgmt.c / bluetoothd
```
**整條路只要有一個環節斷掉，控制就會「假死」**

## 2. mgmt socket：Bluetooth 的「控制面 API」

### 2.1 為什麼需要 mgmt？

歷史背景簡化版：

-   早期：user space 直接送 raw HCI command
    
-   問題：
    
    -   policy 混亂
        
    -   race condition
        
    -   多 process 控制同一顆 controller
        
-   解法：
    
    -   kernel 統一管理 HCI device state
        
    -   user space 只送「意圖」
        
**mgmt 就是「意圖層（intent layer）」**

### 2.2 mgmt 的入口點（Kernel）

檔案：

`net/bluetooth/mgmt.c` 

核心入口：

`static  int  mgmt_cmd(struct sock *sk, struct msghdr *msg, size_t len)` 

這裡做的事：

1.  解析 mgmt command header
    
2.  依 opcode 分派 handler
    
3.  對應到 HCI 動作（通常是送 HCI command）
    
### 2.3 常見 mgmt command 與用途

| mgmt opcode               | 意義             |
|---------------------------|------------------|
| MGMT_OP_SET_POWERED       | 開關藍牙電源     |
| MGMT_OP_START_DISCOVERY   | 開始裝置掃描     |
| MGMT_OP_STOP_DISCOVERY    | 停止掃描         |
| MGMT_OP_CONNECT           | 建立連線         |
| MGMT_OP_SET_LE            | 啟用 Low Energy  |

**btmgmt** 工具就是直接在打這些 mgmt command  
完全不經過 bluetoothd

## 3. 從 mgmt 到 HCI：轉換的關鍵節點

### 3.1 以「power on」為例

User space：

`btmgmt power on` 

Kernel flow（簡化）：
```
mgmt_set_powered()
  └─ hci_dev_do_open()
       └─ hci_open_dev()
            └─ hci_power_on()
```

### 3.2 `hci_power_on()` 在做什麼？

位置：

`net/bluetooth/hci_core.c` 

關鍵行為：

-   檢查 hci_dev state
    
-   送出一系列必要的 HCI command：
    
    -   HCI Reset
        
    -   Read Local Version
        
    -   Set event mask
        
    -   LE setup（如果支援）
        
**如果這裡任何一個 command 沒完成 → power on 卡住**

## 4. HCI Command Queue 機制（為什麼會 timeout）

### 4.1 HCI command 不是「立刻送」

HCI core 有自己的 command queue：

-   同時間只允許有限數量 pending command
    
-   每個 command 需要等：
    
    -   `Command Complete`
        
    -   或 `Command Status`
        
關鍵資料結構：
```
struct hci_dev {
    struct sk_buff_head cmd_q;
    struct sk_buff *sent_cmd;
    ...
};
```

### 4.2 `hci_cmd_sync()` 的同步語意

常見 pattern：

`err = hci_cmd_sync(hdev, opcode, plen, param, timeout);` 

實際流程：

1.  封裝 HCI Command skb
    
2.  丟進 command queue
    
3.  睡眠等待 completion
    
4.  在 event handler 中被喚醒
    
**timeout 的本質**

> command 有送，但對應的 event 沒回來

## 5. HCI Event：完成控制流程的最後一哩

### 5.1 Event 解析入口

檔案：

`net/bluetooth/hci_event.c` 

核心入口：

`void  hci_event_packet(struct hci_dev *hdev, struct sk_buff *skb)` 

這裡會：

-   parse event code
    
-   分派到對應 handler
    
-   更新 hci_dev state
    
-   完成 pending command
    
### 5.2 關鍵事件：Command Complete / Status

| Event              | 意義說明                                   |
|--------------------|--------------------------------------------|
| Command Complete   | Command 已執行完成，並回傳最終結果         |
| Command Status     | Command 已被 Controller 接受，稍後完成     |

如果這兩個 event **任一沒回來**：

-   `hci_cmd_sync()` 永遠等不到
    
-   表現出來就是：
    
    -   `hciconfig hci0 up` 卡住
        
    -   btmgmt power on timeout
        
## 6. btmon：把控制流程「實體化」的工具

### 6.1 btmon 能看到什麼？

-   HCI Command（Host → Controller）
    
-   HCI Event（Controller → Host）
    
-   ACL data（資料面）
    
### 6.2 用 btmon 對照 control path

典型健康流程：

```text
> HCI Command: Reset < HCI Event: Command Complete (Reset)

> HCI Command: Read  Local  Version < HCI Event: Command Complete
```

異常流程（最常見）：

`> HCI Command: Reset (no event)` 

**這一刻就可以直接斷定：不是 BlueZ 的問題**

## 7. 常見問題與排查

### 7.1 常見失敗模式 × 對應卡點

#### command 有送，event 沒回

高機率原因：

-   UART baud rate mismatch
    
-   UART RTS/CTS flow control 問題
    
-   firmware 尚未載入 / controller 還在 ROM
    
-   transport driver 沒真的送出去
    
優先檢查：

```text
drivers/bluetooth/hci_uart.c
drivers/bluetooth/btusb.c
```

#### event 回來，但 status 非 0

代表：

-   controller 拒絕該 command
    
-   firmware 不支援該 opcode
    
-   controller 狀態不對（尚未 ready）
    
#### mgmt command 沒進到 HCI

可能原因：

-   hci_dev state 不允許
    
-   adapter 尚未註冊完成
    
-   先前 command queue 卡死
    
### 7.2 Debug Control Path 的「標準流程」

建議你之後都照這個順序：

1.  停 bluetoothd
    
2.  用 `btmgmt power on`
    
3.  同時開 `btmon`
    
4.  看：
    
    -   command 有沒有送
        
    -   event 有沒有回
        
5.  再決定要不要看 BlueZ
    
**不要一開始就怪 BlueZ**
