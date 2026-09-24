# BSP Firmware Loading（韌體載入與整合實務）

> 本章定位：
> 
> -   站在 **BSP / SoC Bring-up Engineer** 視角，理解 Linux 中 firmware 是如何被載入、管理與更新
>     
> -   說清楚為什麼「driver probe 卡住 / 裝置行為異常」常常其實是 firmware 問題
>     
> -   能實際用於 debug：firmware 找不到、版本不對、載入時序錯誤
>     

## 1. 為什麼 Firmware 是 BSP Bring-up 的最後一塊拼圖

在 BSP bring-up 流程中：

-   bus / interface OK
-   clock / power OK
-   driver 能 probe    

但裝置仍可能：

-   功能不完整
-   行為不穩定
-   初始化卡住    

**很多裝置「真正跑起來」的前提，是 firmware 成功載入。**

## 2. Linux Firmware Framework 的角色

Linux 對 firmware 的基本假設是：

> Kernel 不內建大型 firmware，而是在 runtime 載入。

因此提供一套標準機制：

-   `request_firmware()`
-   userspace helper（udev / systemd）
-   `/lib/firmware` 檔案系統

## 3. Firmware 載入的實際流程

```text
driver probe()
    ↓
request_firmware()
    ↓
userspace helper
    ↓
讀取 /lib/firmware/<name>
    ↓
firmware 傳回 kernel
```

關鍵點：

-   firmware 載入是 **同步或非同步**
-   依賴 userspace 是否 ready

## 4. 為什麼 Firmware 常造成 Probe 卡住

### 4.1 userspace 尚未 ready

在早期 boot：
-   rootfs 尚未 mount
-   udev 尚未啟動

此時 request_firmware 可能：
-   block    
-   timeout

### 4.2 Firmware 檔案不存在或路徑錯誤

常見錯誤：
-   檔名拼錯
-   firmware 未被打包進 rootfs

結果：
-   driver probe 失敗  
-   裝置功能受限

## 5. Device Tree 與 Firmware 名稱

### 5.1 DTS 的責任

DTS 常用來：
-   指定 firmware 名稱

但 DTS：
-   不負責 firmware 是否存在
-   不保證載入成功

### 5.2 常見陷阱

-   firmware 名稱與實際檔名不一致
-   DTS 更新後忘記更新 rootfs

## 6. Firmware 與 Suspend / Resume

部分裝置：
-   resume 時需要重新載入 firmware
-   或重新初始化內部狀態

若 driver 未正確處理：
-   resume 後裝置無反應

**這是 BSP 常見但容易被忽略的問題。**

## 7. Firmware Debug Toolbox

### 7.1 確認 firmware 是否存在

```bash
ls /lib/firmware
ls /lib/firmware/<vendor>/
```

### 7.2 觀察 kernel 訊息

```bash
dmesg | grep -i firmware
```

常見訊息：

-   firmware: failed to load 
-   Direct firmware load failed

### 7.3 驗證載入時序

```bash
cat /proc/cmdline
```

觀察是否：

-   使用 initramfs
-   rootfs 掛載過晚

### 7.4 手動觸發載入

```bash
echo 1 > /sys/module/firmware_class/parameters/path
```

或重新 bind driver 觀察行為。

## 8. 常見問題與排查（常見誤判與責任歸屬）

| 現象           | 常見誤判      | 真正原因               |
|----------------|---------------|------------------------|
| Probe 卡住     | Driver bug    | Firmware blocking      |
| 裝置功能不全   | 硬體問題      | Firmware mismatch      |
| Resume 後失效  | PM bug        | Firmware 未重新載入    |
