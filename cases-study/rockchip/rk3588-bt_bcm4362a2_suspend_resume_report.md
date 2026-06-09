# BCM4362A2 Bluetooth suspend/resume 問題分析與修正說明

## 1. 摘要

本報告整理 BCM4362A2 combo chip 在 Linux 系統 suspend/resume 後 Bluetooth 無法正常恢復的問題。

原本系統使用 `brcm_patchram_plus1` 在開機時下載 Broadcom `.hcd` firmware 並初始化 UART HCI。此流程在 boot-time bring-up 可以正常建立 `hci0`，但在 suspend/resume 後，Bluetooth controller 可能進入異常 UART/HCI 狀態，導致 `HCI_RESET` 收到 garbage data，且單純 rfkill cycling 無法恢復。

修正方式改為使用：

```sh
btattach -B /dev/ttyS9 -P bcm -S 3000000
```

並啟用 kernel Broadcom HCI UART driver 相關設定：

```text
CONFIG_SERIAL_DEV_BUS=y
CONFIG_BT_HCIUART_SERDEV=y
CONFIG_BT_HCIUART_BCM=y
```

此修正的重點不是「`brcm_patchram_plus1` 完全不會呼叫 kernel」。實際上，`brcm_patchram_plus1` 會透過 tty、termios、read/write、ioctl 等方式與 kernel 互動，也可能建立 `hci0`。

真正的差異是：

```text
舊流程：
    brcm_patchram_plus1 是 userspace-driven firmware download / UART setup flow。
    它可以完成 boot-time initialization，但不一定讓 controller 進入
    kernel hci_bcm.c 的 Broadcom HCI UART lifecycle。

新流程：
    btattach -P bcm 會明確選擇 HCI_UART_BCM，
    讓 UART transport 進入 kernel hci_uart + hci_bcm.c path。
    因此 Broadcom-specific suspend/resume handling 可以參與 controller state management。
```

---

## 2. 問題現象

原本的 Bluetooth 初始化流程如下：

```sh
connmanctl enable bluetooth

sleep 2

bt_firmware_path=/lib/firmware/BCM4362A2.hcd
brcm_patchram_plus1 --enable_hci --no2bytes --use_baudrate_for_download \
    --tosleep 200000 --baudrate 3000000 \
    --patchram $bt_firmware_path /dev/ttyS9 &

sleep 2

hciconfig hci0 up
```

在正常開機時，這個流程通常可以完成：

```text
download firmware
        |
        v
initialize UART HCI
        |
        v
create hci0
        |
        v
hciconfig hci0 up
```

但在 suspend/resume 後，可能出現：

```text
Bluetooth 無法恢復
HCI_RESET 收到 garbage data
rfkill block/unblock 無法恢復
重新跑原本 bt-init.sh 不一定有效
```

此時問題已經不是單純 BlueZ 或 rfkill 狀態，而是 controller、UART HCI transport、kernel HCI state 之間可能不同步。

---

## 3. 原本流程：brcm_patchram_plus1

### 3.1 brcm_patchram_plus1 的定位

`brcm_patchram_plus1` 並不是完全被淘汰的工具。它在許多 vendor BSP、Android bring-up、舊 kernel 或客製平台中仍然常見。

它的主要用途是：

```text
下載 Broadcom .hcd patchram firmware
設定 UART baudrate
送 Broadcom vendor command
處理部分 reset / LPM / board-specific 初始化流程
建立或協助建立 HCI interface
```

很多廠商會自行維護 `brcm_patchram_plus1`，原因是 BSP bring-up 常需要快速支援不同 module 或板級差異，例如：

```text
BT_REG_ON GPIO
WL_REG_ON / shared power
reset GPIO
firmware path auto-detect
baudrate 切換
LPM 設定
PCM / SCO routing
BDADDR 設定
vendor-specific command workaround
```

因此，不能簡化成：

```text
brcm_patchram_plus1 已經完全被捨棄
```

比較準確的定位是：

```text
brcm_patchram_plus1：
    適合 boot-time firmware download、vendor BSP bring-up、
    舊平台、Android 客製流程、快速 workaround。

btattach -P bcm + hci_bcm.c：
    適合 Linux kernel-managed HCI UART lifecycle，
    尤其是需要處理 suspend/resume 的情境。
```

### 3.2 brcm_patchram_plus1 不是完全不進 kernel

需要特別釐清：`brcm_patchram_plus1` 不是完全不呼叫 kernel。

它通常會透過以下方式與 kernel 互動：

```text
open("/dev/ttyS9")
termios 設定 UART baudrate / raw mode
read() / write() 傳送 HCI command、vendor command、firmware data
ioctl() 控制 tty 或 HCI UART attach 行為
```

所以問題不是：

```text
brcm_patchram_plus1 = 完全 userspace，不會碰 kernel
btattach = 會碰 kernel
```

真正的問題是：

```text
誰是 Broadcom UART Bluetooth controller lifecycle 的 owner？
```

### 3.3 舊流程

原本流程可以整理成：

```text
bt-init.sh
        |
        v
connmanctl enable bluetooth
        |
        v
brcm_patchram_plus1 ... /dev/ttyS9 &
        |
        v
open /dev/ttyS9
        |
        v
設定 UART baudrate / raw mode
        |
        v
下載 BCM4362A2.hcd firmware
        |
        v
送 Broadcom vendor command / HCI command
        |
        v
可能透過 ioctl attach 到 kernel HCI UART layer
        |
        v
建立 hci0
        |
        v
hciconfig hci0 up
```

這個流程可以完成 boot-time initialization，但它有幾個限制：

```text
1. controller setup 主要由 userspace helper 主導
2. systemd service 是 oneshot，沒有持續追蹤實際 HCI UART attach process
3. brcm_patchram_plus1 被放到 background，service 本身很快結束
4. suspend/resume 時，沒有明確的 kernel Broadcom HCI UART lifecycle owner
5. resume 後若 controller 狀態異常，舊流程不一定能完整恢復 kernel HCI state 與 controller state
```

---

## 4. 新流程：btattach -P bcm

修正後的初始化流程為：

```sh
connmanctl enable bluetooth

sleep 1

exec btattach -B /dev/ttyS9 -P bcm -S 3000000
```

參數意義如下：

| 參數 | 說明 |
|---|---|
| `-B /dev/ttyS9` | 將 `/dev/ttyS9` 作為 Bluetooth BR/EDR controller transport |
| `-P bcm` | 使用 Broadcom HCI UART protocol |
| `-S 3000000` | 將 UART speed 設定為 3 Mbps |

`btattach` 的重點不是單純執行 `hciconfig hci0 up`，而是把 tty serial port attach 到 kernel HCI UART subsystem，並指定使用 Broadcom protocol。

### 4.1 btattach 執行流程

簡化流程如下：

```text
btattach -B /dev/ttyS9 -P bcm -S 3000000
        |
        v
open("/dev/ttyS9", O_RDWR | O_NOCTTY)
        |
        v
tcflush(fd, TCIOFLUSH)
        |
        v
設定 UART raw mode
        |
        v
設定 UART speed = 3000000
        |
        v
ioctl(fd, TIOCSETD, N_HCI)
        |
        v
ioctl(fd, HCIUARTSETFLAGS, HCI_UART_RESET_ON_INIT)
        |
        v
ioctl(fd, HCIUARTSETPROTO, HCI_UART_BCM)
        |
        v
ioctl(fd, HCIUARTGETDEVICE)
        |
        v
kernel 建立 / 註冊 hciX
        |
        v
btattach 持續在 foreground 執行
```

### 4.2 關鍵步驟：TIOCSETD -> N_HCI

```text
ioctl(fd, TIOCSETD, N_HCI)
```

這一步會把 tty line discipline 切成 `N_HCI`。

也就是告訴 kernel：

```text
這個 tty 不再只是一般 serial stream。
請把它當成 Bluetooth HCI UART transport。
```

完成後，UART 收到的資料會進入 kernel Bluetooth HCI UART line discipline，而不是一般 tty data path。

### 4.3 關鍵步驟：HCIUARTSETPROTO -> HCI_UART_BCM

```text
ioctl(fd, HCIUARTSETPROTO, HCI_UART_BCM)
```

這是本次修正最重要的差異。

因為使用 `-P bcm`，`btattach` 會明確選擇 `HCI_UART_BCM`。kernel HCI UART core 會使用 Broadcom-specific protocol driver，也就是 `hci_bcm.c` 相關路徑。

新流程變成：

```text
/dev/ttyS9
        |
        v
btattach
        |
        v
TIOCSETD -> N_HCI
        |
        v
HCIUARTSETPROTO -> HCI_UART_BCM
        |
        v
kernel hci_uart core
        |
        v
Broadcom HCI UART protocol driver
        |
        v
hci_bcm.c
        |
        v
hci0 registered to Bluetooth HCI core
```

這代表 Broadcom UART Bluetooth controller lifecycle 會更明確地進入 kernel `hci_bcm.c` 管理路徑。

---

## 5. 差異比較

| 項目 | 舊流程：brcm_patchram_plus1 | 新流程：btattach -P bcm |
|---|---|---|
| 主要定位 | userspace firmware download / vendor bring-up helper | HCI UART attach tool |
| 是否會與 kernel 互動 | 會，透過 tty、termios、read/write、ioctl | 會，透過 tty、line discipline、HCI UART ioctl |
| 是否可能建立 hci0 | 可能 | 會透過 HCI UART attach 建立 |
| 是否明確選擇 HCI_UART_BCM | 不一定，取決於 vendor 實作與使用方式 | 是，`-P bcm` 明確指定 |
| lifecycle owner | userspace-driven init/recovery flow | kernel `hci_uart` + `hci_bcm.c` |
| systemd 追蹤方式 | `Type=oneshot`，helper background | `Type=simple`，foreground process |
| suspend/resume PM handling | 需要額外 vendor workaround / sleep hook | 可走 kernel Broadcom-specific PM path |
| 適合情境 | boot-time bring-up、舊 BSP、Android 客製流程 | Linux kernel-managed UART BT lifecycle、suspend/resume 穩定性 |

---

## 6. Root cause 修正版

較精準的 root cause 如下：

```text
根本原因不是 brcm_patchram_plus1 完全不會呼叫 kernel。
brcm_patchram_plus1 會透過 tty、termios、read/write、ioctl 等方式與 kernel 互動，
廠商 fork 版本也可能加入 reset GPIO、firmware reload、resume recovery 等邏輯。

本問題的重點是：目前這套 brcm_patchram_plus1-based flow 是
userspace-driven initialization/recovery path，並不是 Linux kernel PM framework
中 Broadcom HCI UART device 的 lifecycle owner。

在此 failure case 中，suspend/resume 後 BCM4362A2 controller 的實際狀態
可能與 Linux Bluetooth HCI core 預期狀態不同步。舊流程沒有可靠地透過
Broadcom-specific kernel-managed suspend/resume path 恢復 controller state，
因此 HCI_RESET 可能收到 garbage data。

改用 btattach -P bcm 後，流程會明確選擇 HCI_UART_BCM，並讓 UART transport
進入 kernel hci_uart Broadcom protocol driver，也就是 hci_bcm.c。如此一來，
Broadcom-specific suspend/resume path 可以參與 controller state management。
```

簡短版：

```text
問題不是「userspace vs kernel」，
而是「userspace-driven init/recovery flow」
和「kernel hci_bcm-managed lifecycle」的差異。
```

---

## 7. 為什麼 rfkill cycling 不夠

rfkill 主要控制 Bluetooth radio 從 Linux Bluetooth stack 角度看是 blocked 或 unblocked。它不保證能把 Broadcom combo chip reset 回乾淨的 post-boot UART firmware 狀態。

問題發生的位置比一般 Bluetooth userspace layer 更底層：

```text
BlueZ / bluetoothctl / hciconfig
        |
Linux Bluetooth HCI core
        |
HCI UART line discipline
        |
Broadcom UART controller state
        |
BCM4362A2 combo chip firmware / PM state
```

如果 UART controller 本身已經在異常狀態，切換 rfkill 可能只是在 software-visible radio state 上做切換。它不保證 Broadcom controller 已經重新走過正確的 suspend/resume 或 re-initialization sequence。

---

## 8. 為什麼 brcm_patchram_plus1 不是完全不可行

理論上，廠商可以擴充 `brcm_patchram_plus1` 或搭配 systemd sleep hook 來做 resume recovery，例如：

```text
resume 後：
  1. 停 bluetoothd / connman bluetooth
  2. rfkill block bluetooth
  3. kill 舊的 brcm_patchram_plus1 / hciattach / btattach
  4. detach HCI UART line discipline
  5. 拉 BT_REG_ON / reset GPIO
  6. 重新設定 UART pin / baudrate
  7. 重新下載 BCM4362A2.hcd
  8. 重新 attach HCI UART
  9. hciconfig hci0 up
 10. restart bluetoothd / connman
```

這種方式有可能恢復 Bluetooth，但比較像：

```text
userspace resume recovery workaround
```

而不是：

```text
kernel driver PM lifecycle
```

它的缺點是：

```text
1. 需要自己處理 systemd sleep hook 時序
2. 需要處理 BlueZ / connman / rfkill / hci0 state sync
3. 需要正確 detach / re-attach UART HCI
4. 需要處理 firmware reload 與 reset GPIO timing
5. 需要避免和 kernel HCI core 狀態不同步
6. 每個 BSP / module 可能都要維護不同 workaround
```

因此，對於這次 suspend/resume 問題，比較合理的修正方向是讓 controller 進入 kernel `hci_bcm.c` lifecycle，而不是繼續加大 userspace workaround。

---

## 9. 為什麼 btattach -P bcm 較適合此問題

本問題不是單純 boot-time firmware download，而是 system suspend/resume 後的 controller state recovery 問題。

suspend/resume 涉及：

```text
controller firmware state
UART line discipline state
kernel hci_uart state
kernel hci_dev state
flow control state
wakeup state
BlueZ adapter state
combo chip Wi-Fi/BT shared PM state
```

這些狀態需要清楚的 lifecycle owner。

使用 `btattach -P bcm` 後，流程會明確進入：

```text
kernel hci_uart core
        |
        v
hci_bcm.c
```

因此 Broadcom-specific driver 可以參與 suspend/resume handling，例如：

```text
bcm_suspend_device()
bcm_resume_device()
```

這比在 userspace 事後做 recovery workaround 更符合 Linux Bluetooth HCI UART driver 的設計。

---

## 10. systemd service 調整原因

原本 service：

```ini
[Service]
Type=oneshot
ExecStart=/usr/bin/bt-init.sh
RemainAfterExit=true
```

這種方式適合「跑完就結束」的初始化腳本。

但 `btattach` 是 foreground process，應該持續存在以維持 UART HCI attachment。因此 service 改成：

```ini
[Unit]
Description=Bluetooth Init Service
After=connman.service
StartLimitIntervalSec=60
StartLimitBurst=5

[Service]
Type=simple
ExecStart=/usr/bin/bt-init.sh
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

調整重點：

```text
Type=simple：
    systemd 直接追蹤 foreground btattach process。

Restart=on-failure：
    如果 btattach 異常退出，systemd 可以自動重啟。

StartLimitIntervalSec / StartLimitBurst：
    避免 UART 或設定錯誤時無限快速重啟。
```

`bt-init.sh` 使用：

```sh
exec btattach -B /dev/ttyS9 -P bcm -S 3000000
```

原因是 `exec` 可以讓 shell process 被 `btattach` 取代，使 systemd 追蹤到真正的 Bluetooth attach process。

---

## 11. kernel config 調整原因

新增：

```text
CONFIG_SERIAL_DEV_BUS=y
CONFIG_BT_HCIUART_SERDEV=y
CONFIG_BT_HCIUART_BCM=y
```

### CONFIG_SERIAL_DEV_BUS

啟用 serial device bus infrastructure。

這是 serial-attached devices 透過 kernel serial device framework 管理時需要的基礎設施。

### CONFIG_BT_HCIUART_SERDEV

啟用 Bluetooth HCI UART over serial device framework。

這讓 Bluetooth UART devices 可以更完整地被 kernel 表示與管理。

### CONFIG_BT_HCIUART_BCM

啟用 Broadcom protocol support for HCI UART driver。

這是最關鍵的 Broadcom-specific 部分，會啟用 kernel 中 Broadcom Bluetooth device handling，包括 suspend/resume support。

---

## 12. 修改內容整理

### recipes-connectivity/bt-init-service/files/bt-init.sh

將：

```sh
brcm_patchram_plus1 ... /dev/ttyS9 &
hciconfig hci0 up
```

改成：

```sh
exec btattach -B /dev/ttyS9 -P bcm -S 3000000
```

### recipes-connectivity/bt-init-service/files/bt-init.service

將 systemd service 從 oneshot mode 改成 long-running simple service：

```ini
Type=simple
Restart=on-failure
RestartSec=3
StartLimitIntervalSec=60
StartLimitBurst=5
```

### recipes-kernel/linux/files/bt-hciuart-bcm.cfg

新增 required kernel config fragment：

```text
CONFIG_SERIAL_DEV_BUS=y
CONFIG_BT_HCIUART_SERDEV=y
CONFIG_BT_HCIUART_BCM=y
```

### recipes-kernel/linux/linux-rockchip_6.1%.bbappend

將 config fragment 加入 `SRC_URI`：

```bitbake
SRC_URI += "file://bt-hciuart-bcm.cfg"
```

---

## 13. 建議驗證流程

### 13.1 確認 kernel config

```sh
zcat /proc/config.gz | grep -E 'CONFIG_SERIAL_DEV_BUS|CONFIG_BT_HCIUART_SERDEV|CONFIG_BT_HCIUART_BCM'
```

預期：

```text
CONFIG_SERIAL_DEV_BUS=y
CONFIG_BT_HCIUART_SERDEV=y
CONFIG_BT_HCIUART_BCM=y
```

如果 `/proc/config.gz` 不存在，請檢查 Yocto build output 或 kernel build config。

### 13.2 確認 service 狀態

```sh
systemctl status bt-init.service
```

預期：

```text
Active: active (running)
Main PID: xxxx (btattach)
```

重點：

```text
service 應該是 running
main process 應該是 btattach
不應該像 oneshot service 一樣立即 exit
```

### 13.3 確認 HCI device

```sh
hciconfig -a
```

或：

```sh
bluetoothctl list
```

預期：

```text
可以看到 hci0
controller 可以被 BlueZ 看見
```

### 13.4 確認沒有舊 helper 同時執行

```sh
ps aux | grep -E 'btattach|brcm_patchram|hciattach'
```

預期：

```text
btattach 正在執行
brcm_patchram_plus1 不應該同時管理同一個 UART
同一時間應該只有一個 process 管理 /dev/ttyS9
```

### 13.5 檢查 kernel log

```sh
dmesg | grep -iE 'bluetooth|hci|bcm|ttyS9|uart'
```

建議檢查：

```text
HCI UART attach 是否成功
是否有 Broadcom HCI driver message
是否有 UART error
是否有 baudrate 問題
是否有 resume 後 HCI error
```

### 13.6 suspend/resume 壓力測試

```sh
for i in $(seq 1 20); do
    echo "Suspend/resume test: $i"
    rtcwake -m mem -s 20
    sleep 5
    bluetoothctl list
    hciconfig hci0
done
```

預期：

```text
每次 resume 後 Bluetooth 都還存在
hci0 不會消失
不再觀察到 HCI_RESET 回傳 garbage data
不需要手動 rfkill cycle 才恢復
```

### 13.7 optional：檢查 rfkill

```sh
rfkill list
```

預期：

```text
resume 後 Bluetooth 不應該卡在 blocked
即使 blocked，unblock 後也不應該需要重啟整個 Bluetooth stack
```

---

## 14. 除錯建議

### 14.1 btattach 立即退出

確認 `/dev/ttyS9` 是否存在：

```sh
ls -l /dev/ttyS9
```

確認是否有其他 process 佔用 UART：

```sh
ps aux | grep -E 'btattach|brcm_patchram|hciattach'
```

同一個 Bluetooth UART 應該只由一個 process 管理。

### 14.2 hci0 沒有出現

先檢查 kernel config：

```sh
zcat /proc/config.gz | grep CONFIG_BT_HCIUART_BCM
```

再檢查 log：

```sh
journalctl -u bt-init.service -b
dmesg | grep -iE 'bluetooth|hci|bcm|uart'
```

可能原因：

```text
kernel config 缺少 Broadcom HCI UART support
UART node 錯誤
baudrate 錯誤
pinctrl/UART conflict
firmware 或 controller reset 問題
其他 service 和 bt-init.service race
```

### 14.3 service 一直 restart

檢查 service log：

```sh
journalctl -u bt-init.service -b --no-pager
```

如果 service 一直重啟，通常代表 UART attach command 本身持續失敗，需要先確認 UART node、baudrate、kernel config、pinctrl 與是否有其他 process 佔用。

---

## 15. 結論

`brcm_patchram_plus1` 並不是完全被捨棄，也不是完全不會與 kernel 互動。它仍然常見於 vendor BSP、Android bring-up、舊平台與需要板級客製初始化的情境。

但在本問題中，失敗點不是單純 boot-time firmware download，而是 suspend/resume 後 Bluetooth controller state、UART HCI state 與 kernel Bluetooth state 不同步。

原本的 `brcm_patchram_plus1` flow 屬於 userspace-driven initialization/recovery path。即使它能建立 `hci0`，也不代表它是 Linux kernel PM framework 中 Broadcom HCI UART device 的 lifecycle owner。

改用：

```sh
btattach -B /dev/ttyS9 -P bcm -S 3000000
```

後，流程會透過：

```text
TIOCSETD -> N_HCI
HCIUARTSETPROTO -> HCI_UART_BCM
```

明確讓 UART transport 進入 kernel `hci_uart` Broadcom protocol driver，也就是 `hci_bcm.c`。如此一來，Broadcom-specific suspend/resume path 可以參與 controller state management。

因此，本次修正的核心價值是：

```text
從 userspace-driven Broadcom BT initialization/recovery flow
改成 kernel hci_bcm-managed Broadcom UART Bluetooth lifecycle。
```

這樣的責任邊界較清楚，也更適合處理 suspend/resume 後的 controller state consistency 問題。

