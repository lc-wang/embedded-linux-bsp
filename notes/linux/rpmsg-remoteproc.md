# Remoteproc & RPMsg 解析（Linux Multi-core IPC）

本章介紹 Linux remoteproc 與 rpmsg 子系統，涵蓋：

- remoteproc 架構  
- firmware loading（ELF）  
- virtio-rpmsg IPC  
- Linux <→ M-core / DSP / CR52 通訊  
- sysfs 介面  
- 常見 bring-up 流程（RZ/T2H, i.MX, TI）  
- Debug 與常見錯誤分析  

## 1. Remoteproc 是什麼？

Remoteproc 是 Linux 用來「管理另一顆 CPU（remote processor）」的框架。

Linux 負責：

| 項目 | 說明 |
| --- | --- |
| Firmware loading | 載入 remote core 的 firmware（通常是 ELF） |
| Reset / start / stop | 控制 remote CPU 執行 |
| Resource Table | 共享記憶體、vring queue 配置 |
| Crash handling | remote core crash 時回報 |
| 溝通通道 | 提供 rpmsg IPC |

Remote core（如 ARM Cortex-R、M、DSP）通常執行 FreeRTOS 或裸機程式。

## 2. RPMsg（RemoteProc Message）

RPMsg 是 remoteproc 上層的 IPC 機制，基於 virtio（虛擬裝置）實作。

### 2.1 RPMsg 主要用途

- Linux <-> M-core message passing  
- Camera ISP Firmware communication  
- Wi-Fi / Bluetooth Co-processor IPC  
- Motor control firmware（例如 RZ/T2H CR52）  
- SoC heterogeneous computing  

## 3. Remoteproc + RPMsg 架構

```text
User space
↓ (open /dev/rpmsgX)
RPMsg char driver
↓
virtio_rpmsg_bus
↓
remoteproc core
↓
Remote CPU (CR52 / DSP / M4 / etc.)
```

## 4. Resource Table（Firmware 的核心）

在 remote firmware 中，需放置 `.resource_table`：

```c
struct my_resource_table {
    struct resource_table base;
    uint32_t offset[1];
    struct fw_rsc_vdev rpmsg_vdev;
};
```

### 4.1 用途：

| 資源 | 說明 |
|------|------|
| VDEV | virtio 裝置（rpmsg） |
| VRING | 兩個 ring buffer（tx / rx） |
| CARVEOUT | reserved memory |
| TRACE | remote log buffer |

Linux remoteproc 會讀 resource table 來配置共享記憶體。

## 5. Firmware（ELF）載入流程

remoteproc 啟動流程：

```bash
echo start > /sys/class/remoteproc/remoteprocX/state
```

步驟：

1.  解析 ELF  
2.  建立 carveout（reserved memory）  
3.  設定 vring buffer
4.  設定 IPC 資源 
5.  發送 `start()`  
6.  remote CPU 從 entrypoint 執行

remote firmware 常在 `.text` + `.bss` + `.resource_table`。

## 6. RPMsg Nameservice

当 remote firmware 啟動後，它會發送：

```c
rpmsg_ns_announce("my_rpmsg_service")
```
Linux 則在：

```sh
/sys/bus/rpmsg/devices/
```
生成：

```sh
my_rpmsg_service.0
```
用戶層可透過 /dev/rpmsg0 通訊。

## 7. RPMsg user-space API

RPMsg 以 character device 提供：

```c
int fd = open("/dev/rpmsg0", O_RDWR);
write(fd, "hello", 5);
```
```c
read(fd, buf, sizeof(buf));
```

## 8. Device Tree 設定

remoteproc 需要：

```dts
rproc0: r5f@0 {
    compatible = "ti,am65-r5f";
    memory-region = <&r5f_mem>;
    firmware-name = "r5f_firmware.elf";
};
```

RPMsg 通常需要：
-   reserved-memory（共享記憶體）
-   vring carveout  
-   mbox（mailbox）

你在 RZ/T2H CR52 中也需設定：

```dts
memory-region = <&cr52_reserved>;
```

## 9. sysfs 介面

remoteproc：

```sh
/sys/class/remoteproc/remoteprocX/state
/sys/class/remoteproc/remoteprocX/firmware
/sys/class/remoteproc/remoteprocX/trace0
```
可用：

```sh
echo start > remoteprocX/state
echo stop > remoteprocX/state
```
RPMsg：

```sh
/sys/bus/rpmsg/devices/
```

## 10. 常見 Debug 指令

查看 remoteproc 啟動 log
```sh
dmesg | grep remoteproc
```
查看 RPMsg channel
```sh
ls /sys/bus/rpmsg/devices
```
查看 vring 狀態
```sh
dmesg | grep vring
```

## 11. Remoteproc Bring-up 流程

```text
1. 分割 reserved memory
2. 寫 linker script 給 remote firmware
3. 實作 resource_table
4. DTS 加 memory-region
5. remoteproc driver 啟動
6. firmware 放到 /lib/firmware
7. 開機後 remoteproc start
8. rpmsg 產生 nodes
```

## 12. 常見問題與排查

| 錯誤訊息 | 原因 | 解決方式 |
|----------|------|-----------|
| failed to load resource table | firmware 未包含 `.resource_table` | 修改 linker script |
| no carveout memory region | reserved-memory 未配置 | 加入 DTS `memory-region` |
| /dev/rpmsg0 missing | firmware 未呼叫 `rpmsg_ns_announce` | remote 程式未啟動 |
| vring timeout | interrupt 丟失 / mailbox 未啟動 | 檢查 mbox driver |
| remoteproc stuck in reset | entrypoint 錯誤 | 確認 firmware linker address |

你在 RZ/T2H CR52 遇到：

-   firmware load address 錯
-   rpmsg channel 不產生    
-   reserved memory 重疊    
-   firewall / MPU 造成 crash
