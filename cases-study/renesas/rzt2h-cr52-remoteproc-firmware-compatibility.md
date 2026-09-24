# RZ/T2H — CR52 Firmware 與 Linux RemoteProc 相容性技術分析報告

## 1. 問題概述

在 RZ/T2H 平台中：
-   Cortex-A55 執行 Linux
-   Cortex-R52 通常執行即時控制（motor/FOC/encoder 等）firmware

某些系統架構需求希望：

**由 Linux 透過 remoteproc 啟動 CR52 並使用 OpenAMP（RPMsg）進行互動。**

然而，多數 motor-control 類 firmware **無法直接由 Linux remoteproc 載入與啟動**。  
remoteproc 專用的 CR52 firmware 與一般 motor-control firmware 在結構上存在顯著差異。

本報告整理了兩種 firmware 的差異、remoteproc 的要求、以及使 motor-control firmware 相容的方式。

## 2. 分析過程

### 2.1 remoteproc 對 CR52 firmware 的必要條件

Linux remoteproc framework 對 firmware 有明確需求。若無法滿足這些需求，remoteproc 將無法啟動 CR52 或建立 RPMsg IPC。

#### 2.1.1 必須包含 `.resource_table`

remoteproc 需要從 firmware 中取得：
-   vring 配置（address/size）
-   通訊佇列（buffer pool）
-   通訊通道數
-   trace 資訊（若有）
-   vdev 配置

這些資訊都存放於 `.resource_table` 區段，例如：
```c
.readelf -l <firmware>.elf
  ...
  Section .resource_table
```
若 firmware 缺乏 `.resource_table`：
-   remoteproc 無法建立 vring
-   rpmsg 無法初始化
-   firmware 會被判定為格式不支援   

#### 2.1.2 必須具備 vring / shared-memory 區段

OpenAMP IPC 需要下列共享記憶體配置（以 RZ/T2H 為例）：

| 區段 | 位址範圍 | 用途 |
|------|-----------|------|
| resource_table | 0xE000_0000 | IPC 結構交握 |
| vring0 | 0xE100_0000 | A55 → CR52 |
| vring1 | 0xE105_0000 | CR52 → A55 |
| buffer pool | 0xE120_0000 | RPMsg 訊息存放區 |

若 firmware 缺少對應的 linker 區段（如 `.vring`、`.data_noncache`），OpenAMP IPC 將無法啟動。

#### 2.1.3 程式碼與資料段需符合 remoteproc 的 SYSRAM 記憶體布局

OpenAMP 範例中，CR52 firmware 的記憶體布局位於 SYSRAM：
```text
.text     → 0x10060000
.data     → 0x10062000
.entry    → 0x10061000
```

Device Tree 中 remoteproc 節點也會指定：
```dts
renesas,rz-start_address = <0x10061000>;
```
若 firmware 採用 xSPI Boot 或 motor-control 專用地址（例如 0x40000000 之類的區段），  
remoteproc 無法正確載入或啟動程式碼。

#### 2.1.4 必須初始化 OpenAMP / RPMsg stack

CR52 firmware 啟動後必須主動建立：
```c
`OPENAMP_init();
rpmsg_lite_master_init(...);
rpmsg_lite_create_ept(...);
```
remoteproc 只能啟動 firmware，但不會幫 firmware 建立 IPC。  
若 firmware 缺少這些初始化動作：

-   `/dev/rpmsg*` 不會出現
-   IPC 不會建立
-   A55 無法與 CR52 溝通

## 3. Root Cause 分析

以典型 motor-control firmware 為例：

### 3.1 程式碼定址與啟動方式不同

一般 motor-control firmware：
-   為 xSPI boot 設計
-   為 TCM/SRAM 中固定地址設計
-   不採用 0x1006xxxx SYSRAM layout
-   不包含 remoteproc 所需的可解析 LOAD segments

因此 remoteproc 無法載入正確程式碼段或 entry point。

### 3.2 缺少 `.resource_table`、`.vring`、共享記憶體配置

motor-control firmware 通常：
-   不需要 IPC
-   不使用 OpenAMP 
-   不含 resource_table 區段
-   不含 vring buffer 保留區段

缺少這些元素時，remoteproc 會回報：
```bash
unsupported fw ver
invalid phdr
Image is corrupted
```

### 3.3 缺少 OpenAMP / RPMsg 初始化

motor-control firmware 一般專注於：

-   控制迴圈
-   FOC/encoder 演算法
-   驅動周邊（PWM/ADC）
-   中斷即時性

並未內建 RPMsg / VirtIO stack。

### 3.4 TF-A 與 Linux 的記憶體安全設定可能阻擋 motor firmware loading

若 motor firmware 使用的記憶體屬於 secure / 未 map 區域，remoteproc 會在 ioremap 過程產生 SError。

## 4. 解決方案

### 4.1 若要讓 Motor Firmware 支援 remoteproc，需進行的調整

以下為必要的技術修改：

#### 4.1.1 調整 Linker Script

需加入：
-   SYSRAM 程式碼布局（0x10060000 開始）
-   固定 entry point（0x10061000）
-   `.resource_table` 定址在共享記憶體（0xE0000000）
-   `.vring` buffer 定址（0xE1000000…）

#### 4.1.2 實作 resource_table

需加入符合 OpenAMP 規範的 resource_table 結構：
```c
struct fw_rsc_vdev vdev;
struct fw_rsc_vdev_vring vring0;
struct fw_rsc_vdev_vring vring1;
```

#### 4.1.3 加入 RPMsg / OpenAMP 初始化程式碼

如：
```c
rpmsg_lite_instance_t rpmsg;
rpmsg_lite_endpoint_t ept;
```

#### 4.1.4 配合 Linux Device Tree 的 memory-region

與 DTS/domains 中的記憶體配置一致，例如：
```text
vdev0vring0 → 0xE1000000
vdev0vring1 → 0xE1050000
vdev0buffer → 0xE1200000
```

### 4.2 若不調整 motor firmware，仍可支援 Linux ↔ CR52 IPC

可使用替代方案，不需依賴 remoteproc：

#### 4.2.1 方案：CR52 自主從 xSPI Flash boot motor firmware（不走 remoteproc）

Linux 與 CR52 使用：
-   mailbox (IPC)
-   SCIF  
-   SPI 
-   shared registers

等方式進行通訊。

優點：
-   motor firmware 完全不需改動   
-   不需要 resource_table 
-   不中斷現有 motor/FOC 程式設計 
-   適用於大量工控產品

此模式也是 motor-control 系統中最普遍的設計方式。

## 5. 結論與建議

| Firmware 類型 | 適用 remoteproc？ | 說明 |
|------------------------------|----------------|--------|
| OpenAMP / RPMsg demo firmware | ✓ 是 | 完整包含 resource_table、vring、SYSRAM 布局、OpenAMP 初始化 |
| 一般 Motor-Control Firmware (FOC/ENC/PWM) | ✗ 否 | 無 resource_table、無 OpenAMP、記憶體布局不符、無 IPC |
