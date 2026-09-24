
# SoC Boot Sequence

本章深入說明 SoC 開機流程，從 Boot ROM 到 Kernel 的每個階段，  
包含 Bootloader 的分層、裝置啟動來源（Boot Media）、DTB 載入與傳遞等。  
此流程為 Android / Linux BSP bring-up 的基礎。

---

## 1. 開機流程總覽
```yaml
[Boot ROM]
↓
[SPL] （Secondary Program Loader）
↓
[U-Boot Proper]
↓
[Load DTB, Kernel, initramfs]
↓
[Linux Kernel Start]
↓
[Init]
```

| 階段 | 所屬層級 | 功能 |
| --- | --- | --- |
| Boot ROM | SoC 內部電路 | 決定啟動裝置、載入 SPL |
| SPL | U-Boot 子階段 | 初始化 DDR、PMIC、串口 |
| U-Boot Proper | Bootloader 主體 | 載入 Kernel / DTB / initrd |
| Kernel | OS | 硬體初始化、啟動 init |
| Init | User-space | 啟動 zygote/system server |

---

## 2. Boot ROM（第一階段）

Boot ROM 是 SoC 燒在晶片內的固定程式，無法修改。

### 主要工作
- 認證並讀取下一階段程式（SPL）
- 決定啟動來源（boot media）
- 設定暫存器、基礎 clock
- 讀取 eFuse / OTP（安全啟動用）

### 常見 Boot Media
| Boot Media | 說明 |
| --- | --- |
| eMMC / SD | 最常見 |
| SPI NOR / SPI NAND | 工控與安全啟動常見 |
| USB OTG | Recovery / Download mode |
| UART | ROM bootloader 開發模式 |

---

## 3. SPL（Secondary Program Loader）

SPL 是精簡版 U-Boot，通常放在 boot 設備最前面幾 KB。

### 任務
| 功能 | 說明 |
| --- | --- |
| 初始化 DDR | **最重要**：讓系統擁有可用 RAM |
| 初始化 PMIC / 電源 | 讓 SoC 工作在正常電壓下 |
| 初始化最基本的串口 | 輸出 boot log |
| 載入 U-Boot Proper | 從儲存裝置載入至 DDR |

### SPL 在專案中位置
```yaml
u-boot/
├── spl/
└── arch/arm/mach-*/spl.c
```
---

## 4. U-Boot Proper（完整 Bootloader）

U-Boot Proper 是完整 bootloader 主體。

### 主要工作
- 解析 bootcmd
- 初始化各類裝置（MMC、USB、ETH）
- 載入 kernel、DTB、ramdisk
- 設定 bootargs（傳給 kernel）
- 跳轉至 kernel entrypoint

### 常用環境變數
| 名稱 | 功能 |
| --- | --- |
| `bootargs` | Kernel cmdline |
| `fdtaddr` | DTB 加載位置 |
| `kernel_addr_r` | Kernel load address |
| `initrd_addr_r` | initramfs load address |
| `bootcmd` | 啟動邏輯主流程 |

---

## 5. DTB（Device Tree Blob）載入流程

Bootloader 會將 DTB 載入記憶體後傳給 Kernel。

流程：
```yaml
U-Boot
↓ load dtb
↓ fdt addr ${fdtaddr}
↓ fdt resize / apply overlay (optional)
↓ booti / bootz / booto
Kernel
```

### Kernel 接受 DTB 時的行為
- 驗證 **magic number**
- 建立 device node
- 建立 platform_device、匹配 platform_driver
- 設定 memory map
- 設定 reserved-memory
- 匹配 `compatible` → probe 驅動

---

## 6. Kernel 啟動階段（與 Bootloader 連接）

Bootloader 最終會跳轉到 kernel entry。
入口點（ARM64）：
```yaml
arch/arm64/kernel/head.S
```
Kernel 初始化步驟：
1. 建立初始 page table  
2. 啟用 MMU  
3. 初始化 CPU、scheduler  
4. 掛載 initramfs  
5. 啟動第一個 user-space 程式：`/init`

---

## 7. Bootargs（Kernel Command Line）

例：
```yaml
console=ttyS0,115200 root=/dev/mmcblk0p2 rw loglevel=4
```

常用參數：
| bootarg | 說明 |
| --- | --- |
| `console=` | kernel log 輸出設備 |
| `root=` | rootfs 裝置 |
| `rootwait` | 等待 rootfs ready |
| `loglevel=` | kernel log 等級 |
| `earlycon` | early printk |
| `no_console_suspend` | suspend 時保持 UART |

這些參數在 **boot hang / early crash** 排查時非常重要。

---

## 8. U-Boot → Kernel Debug 方法

### 1. 開啟 earlycon
```shell
earlycon=uart8250,mmio32,0xff1a0000
```
### 2. 開啟 initcall debug
```shell
initcall_debug
```

### 3. 顯示每個驅動 probe 時間
```shell
printk.devkmsg=on
```

### 4. 若 Kernel crash
```shell
dmesg -n 8
echo c > /proc/sysrq-trigger
```
---

## 9. 常見問題與排查

| 問題 | 可能原因 | 解決方式 |
| --- | --- | --- |
| Boot ROM 卡住 | Boot media 無效 | 檢查 eMMC / SPI 驅動、fuse 設定 |
| SPL 無法啟動 | DDR 設定錯誤 | 檢查 DDR timing / vendor tuning config |
| U-Boot 卡住 | 電源 / PMIC 未初始化 | 開 UART debug，確認 PMIC log |
| Kernel 無法啟動 | bootargs 錯誤、DTB 無效 | 檢查 DTB load address 與 bootargs |
| 停在 early boot | 未開 earlycon | 加入 `earlycon` bootarg |
| Kernel probe driver 失敗 | device tree `compatible` 不匹配 | 檢查驅動 of_match_table |

---

**延伸閱讀**
- U-Boot 官方文件: https://u-boot.readthedocs.io  
- Linux ARM64 Booting: `Documentation/arm64/booting.rst`  
- Device Tree Spec v0.4  

