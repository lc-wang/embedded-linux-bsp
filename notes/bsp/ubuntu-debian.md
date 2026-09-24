# Ubuntu / Debian BSP 實作與系統架構筆記

本章整理 Ubuntu / Debian 在 BSP 開發中的必備知識，包括：

- rootfs 架構與檔案系統設計  
- initramfs、init system、systemd  
- 交叉編譯流程與 multiarch  
- kernel packaging (.deb)  
- SD/eMMC/NVMe boot integration  
- 常見 debug 技巧  
- BSP bring-up 流程（Renesas / Rockchip / i.MX）  

## 1. Ubuntu / Debian 系統架構

Ubuntu 與 Debian 皆基於：

| 元件 | 說明 |
| --- | --- |
| `dpkg` | 最底層 package manager |
| `APT` | 提供 repository 與 dependency resolver |
| `systemd` | init system（PID 1） |
| `/etc/init.d` | 傳統 SysV init script（仍有支援） |
| `/lib/modules/$(uname -r)` | kernel module 路徑 |
| `/boot` | kernel + initrd + config |

## 2. Rootfs 結構（Debian/Ubuntu）

典型 rootfs：
```yaml
rootfs/
├── bin/
├── sbin/
├── etc/
├── lib/
├── usr/
├── var/
├── opt/
├── root/
├── boot/ ← kernel / initrd
├── lib/modules/← kernel modules
└── dev/ proc/ sys/ run/
```

### 2.1 BSP 最常改動的部位

| 位置 | 說明 |
| --- | --- |
| `/boot` | kernel / dtb / initrd |
| `/etc/fstab` | 掛載分區 |
| `/etc/network/interfaces` or netplan | eth0 / wlan0 |
| `/etc/modules-load.d` | 上層要自動載入的 module |
| `/lib/firmware` | Wi-Fi / BT / ISP firmware |

## 3. initramfs 與 rootfs 的差別

```markdown
initramfs
↓ (kernel 解壓)
init
↓
真正的 rootfs (/dev/mmcblk0p2, /dev/nvme0n1p2)
```

### 3.1 initramfs 用途

- 掛載真正 rootfs
- 提供 early debug（busybox, sh）
- LUKS、LVM、RAID
- Kernel module 早期載入

### 3.2 生成 initramfs

```bash
update-initramfs -c -k <version>
```
Ubuntu/Debian 在嵌入式 BSP 中常需手動修改：
```yaml
/etc/initramfs-tools/initramfs.conf
/etc/initramfs-tools/modules
```

## 4. systemd（PID 1）

systemd 是 Ubuntu/Debian 的核心 init system。

### 4.1 檢查 boot 時序

```bash
systemd-analyze
systemd-analyze blame
```

### 4.2 加入自訂 service

```bash
/etc/systemd/system/myapp.service
```
範例：

``` ini
[Unit]
Description=My Daemon
After=network.target

[Service]
ExecStart=/usr/bin/mydaemon
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

## 5. Kernel .deb 打包與安裝

Ubuntu/Debian 的 kernel 不使用原始 Image，而使用 .deb 套件。

編譯並生成 .deb
```bash
make -j$(nproc) bindeb-pkg
```
產物：

```bash
linux-image-<version>.deb
linux-headers-<version>.deb
```
安裝：
```bash
dpkg -i linux-image-*.deb
dpkg -i linux-headers-*.deb
```

### 5.1 注意：

-   Kernel modules 在 `/lib/modules/<version>/`
-   Bootloader 需能讀取 `/boot/vmlinuz-*`（GRUB / U-Boot）

## 6. BSP Bring-up 流程（Ubuntu/Debian）

嵌入式 BSP bring-up 大多是：

```markdown
U-Boot
  ↓
Kernel (Image.gz + dtb)
  ↓
Ubuntu / Debian rootfs
```

### 6.1 標準流程

1.  **準備 rootfs**

```bash
debootstrap --arch=arm64 focal rootfs/
```
2.  **加入必要工具**

```bash
apt install sudo udev net-tools ethtool
```
3.  **放入 kernel / dtb / modules**

```bash
cp Image rootfs/boot/
cp *.dtb rootfs/boot/
cp -r lib/modules rootfs/lib/
```
4.  **設定 fstab**
```bash
/dev/mmcblk0p2 / ext4 defaults 0 1
```
5.  **建立使用者**
```bash
chroot rootfs adduser ubuntu
```
6.  **產生 SD image**

```bash
dd if=/dev/zero of=ubuntu.img bs=1M count=4096
mkfs.ext4 ubuntu.img
mount -o loop ubuntu.img /mnt
cp -a rootfs/* /mnt
```

## 7. Network / Wi-Fi / BT Bring-up

Ubuntu/Debian 使用：
-   `systemd-networkd`
-   netplan
-   `/etc/network/interfaces`（legacy）

### 7.1 以 eth0 為例：

`/etc/netplan/01-netcfg.yaml`：

```yaml
network:
  version: 2
  ethernets:
    eth0:
      dhcp4: true
 ```
Wi-Fi（wpa_supplicant）：

```bash
sudo wpa_passphrase ssid password > /etc/wpa_supplicant/wpa_supplicant.conf
systemctl enable wpa_supplicant
```

## 8. eMMC / SD / NVMe boot（U-Boot + Debian）

U-Boot example：

```bash
setenv bootargs "root=/dev/mmcblk0p2 rw rootwait console=ttyAMA0,115200"
ext4load mmc 0:1 ${kernel_addr_r} /boot/Image
ext4load mmc 0:1 ${fdt_addr_r} /boot/r9a09g077.dtb
booti ${kernel_addr_r} - ${fdt_addr_r}
```
NVMe：

```bash
nvme scan
load nvme 0:1 ${kernel_addr_r} /boot/Image
```

## 9. Common Debug Techniques

**檢查 rootfs 問題**
```yaml
journalctl -xb
systemctl --failed
dmesg | grep mmc
```
**進 initramfs debug**
GRUB 或 U-Boot：

```ini
break=mount
```
Ubuntu 會 drop 到 busybox。

**Wi-Fi 無法啟動**
```bash
dmesg | grep firmware
ls /lib/firmware
```

## 10. 常見問題與排查

| 問題 | 可能原因 | 解決方式 |
|------|-----------|------------|
| rootfs 無法開機 | fstab 錯誤、initrd 缺少 module | 加入 mmc/nvme driver |
| 無法登入 | 無 user/pass | 設定 `/etc/shadow` 或安裝 openssh |
| Wi-Fi 無法啟動 | firmware 不在 `/lib/firmware` | 放入 vendor 提供的 bin |
| systemd 卡住 | 驅動 module 掛掉阻塞 | `systemd-analyze blame` |
| SD boot err -110 | SDHCI timing 問題 | 調整 DTS bus-width / UHS |
