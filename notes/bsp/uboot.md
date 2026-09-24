# U-Boot Overview and Boot Flow 
這份筆記整理 U-Boot (Universal Bootloader) 的主要架構與啟動流程，  
說明 U-Boot 在 BSP 中的角色、常見命令、環境變數與 Kernel 交互方式。

---

## 1. 什麼是 U-Boot？
- **U-Boot (Das U-Boot)** 是開源的嵌入式系統 bootloader。  
- 負責 SoC 啟動後的第一階段初始化與作業系統載入。  
- 支援多平台 (ARM / RISC-V / PowerPC / x86)。  

| 階段 | 主要任務 |
| --- | --- |
| **ROM / BootROM** | 由 SoC 硬體執行，載入 bootloader (如 SPL) |
| **SPL (Secondary Program Loader)** | 初始化記憶體 (DDR)、時鐘、PMIC |
| **U-Boot Proper** | 提供命令列介面、載入 Kernel 或其他 OS |
| **OS (Kernel)** | 接手系統控制，進入正常運行狀態 |

--- 

## 2. 啟動流程總覽

U-Boot 的啟動流程如下：

[SoC ROM]  
  ↓  
[SPL] — 初始化 DDR / Clock / PMIC  
  ↓  
[U-Boot Proper]  
  ↓  
[Load Kernel + DTB + Initramfs]  
  ↓  
[Jump to Kernel Entry]  
  ↓  
[Linux Kernel Start]

--- 
## 3. SPL (Secondary Program Loader)
- 位於 `spl/` 目錄，為精簡版 U-Boot。  
- 功能：初始化最基本的硬體環境，使 U-Boot Proper 能運行。  
- 常見任務：
  - 初始化 Stack / BSS
  - 設定 PLL 與 Clock
  - 初始化 DRAM
  - 從儲存媒介 (eMMC / SD / SPI-NOR) 載入 U-Boot Proper
- 典型輸出檔案：
```c
spl/u-boot-spl.bin  
spl/u-boot-spl.elf
```
 --- 
 ## 4. U-Boot Proper
U-Boot 主體程式，提供完整命令列與網路支援。

| 功能 | 說明 |
| --- | --- |
| CLI 指令 | boot, load, printenv, setenv, saveenv, mmc, tftp 等 |
| 網路協定 | 支援 TFTP / DHCP / NFS boot |
| 檔案系統 | 支援 FAT, EXT4, UBIFS 等 |
| 驅動模型 | 使用 Driver Model (DM) 與 Device Tree 初始化設備 |
| 開機控制 | 透過 bootcmd、bootargs 控制 Kernel 啟動行為 | 
---
## 5. 重要環境變數

| 變數 | 說明 |
| --- | --- |
| `bootcmd` | U-Boot 開機後自動執行的指令序列 |
| `bootargs` | 傳遞給 Linux Kernel 的命令列參數 |
| `bootdelay` | 等待使用者中斷自動啟動的秒數 |
| `loadaddr` | 載入映像檔的記憶體位址 |
| `kernel_addr_r` / `fdt_addr_r` / `ramdisk_addr_r` | 對應 Kernel、DTB、Initramfs 的載入位址 |
| `fdtfile` | 指定要使用的 Device Tree 檔案 |
| `ethaddr` | 以太網卡 MAC 位址 |
| `stdin/stdout/stderr` | 控制輸入/輸出設備 (如串口、顯示) |
 ---
## 6. 常見指令

| 指令 | 功能說明 |
| --- | --- |
| `printenv` | 顯示目前所有環境變數 |
| `setenv <var> <val>` | 設定環境變數 |
| `saveenv` | 將變數存入儲存媒介 (如 eMMC) |
| `mmc list / mmc info` | 檢查 MMC 裝置 |
| `load mmc 0:1 0xC0000000 Image` | 從 SD/eMMC 載入 Kernel |
| `tftpboot <addr> <file>` | 透過 TFTP 載入檔案 |
| `booti <kernel> - <fdt>` | 啟動 Linux (arm64 使用 `booti`) |
| `bootm` | 啟動 legacy uImage (舊格式) |
| `fatls / ext4ls` | 列出檔案系統內容 |
| `help` | 顯示所有可用命令 |
 --- 

## 7. Device Tree 與 Kernel 傳遞
- U-Boot 通常負責將 **Kernel Image** 與 **Device Tree (DTB)** 一起載入記憶體，  
然後將兩者地址傳給 Kernel。
- 典型流程：
```bash
setenv bootargs 'console=ttyS0,115200 root=/dev/mmcblk0p2 rw rootwait'
load mmc 0:1 ${kernel_addr_r} Image
load mmc 0:1 ${fdt_addr_r} myboard.dtb
booti ${kernel_addr_r} - ${fdt_addr_r}
```
- 若使用 Android 平台，會改由 **boot.img** 與 **boot header** 控制。

----------

## 8. Boot 流程控制實例
```bash
# 顯示預設 boot 流程
printenv bootcmd

# 範例：透過 MMC 啟動
setenv bootcmd 'mmc dev 0; load mmc 0:1 ${kernel_addr_r} Image; \
                load mmc 0:1 ${fdt_addr_r} myboard.dtb; \
                booti ${kernel_addr_r} - ${fdt_addr_r}'
saveenv
```

----------


## 9. U-Boot 與 Kernel 的互動

| 項目 | 說明 |
| --- | --- |
| **傳遞方式** | U-Boot 透過 Device Tree (FDT) 或舊式 ATAGS 將硬體資訊傳給 Kernel。 |
| **命令列參數** | U-Boot 的 `bootargs` 環境變數會轉換為 Kernel 的 `/proc/cmdline`。 |
| **記憶體分配** | Kernel 啟動時根據 U-Boot 設定的 `loadaddr`、`kernel_addr_r`、`fdt_addr_r` 等位址進行對映。 |
| **Device Tree 傳遞** | U-Boot 負責載入 `.dtb` 檔並將位址傳入 Kernel entry point。 |
| **Firmware 介面** | 舊版平台使用 ATAGS，新版平台採用 Device Tree。 |
| **Secure Boot 支援** | 可搭配 ARM TrustZone 驗證 Kernel 映像簽章。 |
| **Android 特殊流程** | Android 平台以 `boot.img` 格式封裝 Kernel + ramdisk + header，由 bootloader 解析後載入。 |

----------


## 10. 常見調試技巧

| 工具 / 方法 | 用途 |
| --- | --- |
| `printenv` / `saveenv` | 檢查與儲存 boot 環境變數設定。 |
| `bdinfo` | 查看記憶體範圍、載入位址、CPU 型號、板子資訊。 |
| `mmc list` / `fatls` | 檢查儲存媒體與分區內容。 |
| `tftpboot` / `ping` | 測試網路連線與 TFTP 下載功能。 |
| `bootdelay=-1` | 停用自動啟動以進行手動除錯。 |
| `bootcount` / `altbootcmd` | 多重啟動策略（失敗回復機制）。 |
| 串口 log | 觀察 SPL、U-Boot、Kernel 啟動過程與崩潰點。 |
| `env default -a` | 重設所有環境變數為預設值（避免錯誤設定）。 |
| `help` / `help <cmd>` | 查詢指令用途與參數說明。 |

----------

## 11. 學習建議

1.  在開發板上練習手動 boot：
```bash
load mmc 0:1 ${kernel_addr_r} Image
load mmc 0:1 ${fdt_addr_r} myboard.dtb
booti ${kernel_addr_r} - ${fdt_addr_r}
```
2.  嘗試修改 `bootargs`，觀察 Kernel log (`dmesg | grep cmdline`)。
3.  研究 `include/configs/<board>.h` 與 `defconfig` 的差異。
4.  編譯自定義 U-Boot，加入你自己的命令 (`cmd_*.c`)。
5.  深入了解 SPL → U-Boot Proper → Kernel 的銜接。
    

----------

**延伸閱讀**

-   [U-Boot 官方文件](https://u-boot.readthedocs.io/en/latest/)
-   `Documentation/arm64/booting.txt`
-   `include/configs/`、`common/bootm.c`
-   Android: `boot.img` header format (`mkbootimg`)
