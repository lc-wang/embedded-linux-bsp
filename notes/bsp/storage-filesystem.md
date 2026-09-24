# Storage & Filesystem in Android/Linux BSP

本章整理 Android 與 Linux BSP 中常見的儲存設備（eMMC、SD、SPI-NAND、NVMe）  
以及分區格式、boot image 結構、GPT/MBR、U-Boot 與 Kernel 如何存取 storage。

## 1. 常見儲存種類

| 儲存 | 說明 | 常見平台 |
| --- | --- | --- |
| **SD卡 (SD/SDHC/SDXC)** | 通用、便宜、速度不穩定 | i.MX、Renesas、RK |
| **eMMC** | 內建 MMC flash、效能穩定 | Android 手機/工控 |
| **UFS** | 高速，Android flagship 標配 | 新款手機 |
| **SPI-NOR** | 小容量 (4M~128M)、高可靠度 | Bootloader 儲存 |
| **SPI-NAND** | 大容量 NAND，包含 ECC | 工控板 |
| **NVMe (PCIe SSD)** | 最高速，需 PCIe | RK3588/i.MX8 |

## 2. Partition Layout（GPT）

Android / Linux BSP 大多使用 GPT。

範例：
```yaml
gpt layout:
0: bootloader
1: reserved
2: boot
3: recovery
4: misc
5: dtbo
6: system
7: vendor
8: userdata
```

### 2.1 分區組成

| 分區 | 功能 |
| --- | --- |
| **boot** | kernel + ramdisk |
| **dtbo** | DT overlay |
| **vendor_boot** | vendor kernel / ramdisk（Android 12+） |
| **system** | framework 與系統程式 |
| **vendor** | HAL、so library、firmware |
| **userdata** | 使用者資料 |

## 3. Boot Image 結構（Android）

Android 的 boot.img 格式（boot header v3 / v4）包含：
```yaml
boot.img
├ header
├ kernel (Image.gz)
├ ramdisk
├ dtb / dtbo
└ vendor_boot（Android 12+）
```

### 3.1 為什麼 Android 12+ 需要 `vendor_boot`？

- 強制 system/vendor 分離
- vendor 需放自己的 ramdisk
- system ramdisk 不可包含 vendor code

## 4. U-Boot 與 Storage

U-Boot 常見儲存命令：

| 命令 | 說明 |
| --- | --- |
| `mmc list` | 列出 SD/eMMC |
| `mmc part` | 查看分區 |
| `mmc read` | 讀取 block |
| `fatload` | 從 FAT 分區讀檔 |
| `ext4load` | 從 ext4 分區讀檔 |
| `nvme scan` | 掃描 NVMe SSD |
| `sf probe` | SPI flash |
| `sf read` | SPI flash 讀取 |

### 4.1 例：從 SD 讀取 kernel

```bash
mmc dev 0
fatload mmc 0:1 ${kernel_addr_r} Image
```

### 4.2 例：NVMe Boot（RK3588）

```bash
nvme scan
load mmc 0:1 ${fdt_addr_r} rk3588.dtb
load nvme 0:1 ${kernel_addr_r} Image
booti ${kernel_addr_r} - ${fdt_addr_r}
```

## 5. Kernel 與儲存驅動

Kernel 模組：

| 設備 | 驅動 | 入口點 |
| --- | --- | --- |
| SD/eMMC | MMC/SDHCI | `/drivers/mmc/` |
| NVMe | PCIe NVMe | `/drivers/nvme/host/` |
| SPI Flash | MTD | `/drivers/mtd/spi-nor/` |
| NAND Flash | MTD + ECC | `/drivers/mtd/nand/` |

## 6. Filesystem 選擇

| FS | 優點 | 缺點 | 適用 |
| --- | --- | --- | --- |
| **ext4** | 穩定、快速、支援 journal | 效能不如 f2fs | Android rootfs |
| **f2fs** | 專為 flash 設計 | 工控較少用 | userdata |
| **squashfs** | readonly、高壓縮率 | 不能寫入 | recovery/system |
| **ubifs** | NAND 專用 | 僅 MTD | SPI-NAND |
| **erofs** | fast + readonly | 不可寫 | Android system（Android 13+） |

## 7. initramfs / rootfs / system.img

### 7.1 rootfs 種類

| 類型 | 說明 |
| --- | --- |
| initramfs | 內建到 kernel 的壓縮 cpio |
| system.img | Android system 分區 |
| rootfs.tar.gz | Yocto、Debian 的 rootfs |

### 7.2 Kernel 指定 rootfs

```yaml
root=/dev/mmcblk0p2 rw rootwait
```

## 8. Storage Debug

### 8.1 查看分區

```bash
lsblk
fdisk -l
blkid
```

### 8.2 dump 開頭幾個 block

```bash
hexdump -C /dev/mmcblk0 | head
```
（用來 debug SD boot 問題）

### 8.3 Kernel MMC log

```yaml
dmesg | grep mmc
```

常見問題：
- `Card did not respond to voltage select (err -110)`
- `mmc: timeout waiting for hardware interrupt`

## 9. 常見問題與排查

| 問題 | 可能原因 | 建議處理 |
| --- | --- | --- |
| SD 16G 能開機，32G 無法 | 卡 voltage switch / timing | 禁用 UHS (`no-1-8-v`) |
| Rootfs 無法掛載 | partition layout 錯 | 檢查 `/proc/cmdline` |
| U-Boot load kernel 失敗 | offset 錯誤 | 查看 GPT 分區起始位置 |
| Kernel 找不到 NVMe | PCIe reset 未拉高 | 檢查 device tree / power sequence |
| random I/O 慢 | SD 卡品質差 | 換工控級 SD/eMMC |
