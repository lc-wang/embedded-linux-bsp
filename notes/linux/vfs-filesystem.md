# Linux VFS & Filesystem Architecture

本章介紹 Linux Virtual File System (VFS)、inode/dentry/page cache、mount 流程、系統啟動時的 rootfs 選擇機制、以及不同 filesystem 特性比較。亦包含實際 BSP / Android / Yocto 常遇到的 storage 問題分析方法。

## 1. Linux VFS 架構

```text
VFS (Virtual File System) 是所有檔案系統的抽象層：

Userspace (glibc, open/read/write)
↓
VFS (syscall interface)
↓
Filesystem driver (ext4, f2fs, squashfs, erofs)
↓
Block layer
↓
Storage device (MMC/eMMC/UFS/NVMe)
```

VFS 提供統一 API：

- `open()`
- `read()`
- `write()`
- `ioctl()`
- `mmap()`

並讓不同 filesystem 可以共用：

- dentry cache
- page cache
- file_operations
- inode operations

## 2. 核心資料結構

| 結構 | 功能 |
| --- | --- |
| **inode** | 檔案的 metadata（permission, owner, size, timestamps） |
| **dentry** | path component（目錄項目），加速查詢路徑 |
| **super_block** | filesystem 的全域描述 |
| **file** | process 對檔案的開啟操作（open fd） |
| **address_space** | page cache 管理器 |

資料流示意：
```text
path → dentry lookup → inode → page cache → block device
```

## 3. 檔案操作流程

### 3.1 Open

```text
open("/etc/passwd")
→ dentry lookup
→ 找到 inode
→ 返回 struct file*
```

### 3.2 Read

```text
read(fd)
→ file_operations->read_iter()
→ page cache
若 miss → block read
```

### 3.3 Write

```text
write(fd)
→ page cache (dirty page)
→ background flusher 寫回 block
```

## 4. Page Cache

Page cache 讓 filesystem 的讀寫高速化：

- 讀取時先查 page cache  
- 寫入時在 cache 做 dirty marking  
- kernel 的 flush daemon 寫回磁碟  

Page cache 也負責 mmap：
```text
mmap() → share underlying page cache
```

## 5. Mount 與 Rootfs 選擇流程

Boot 時 kernel 根據 `root=` 參數選擇 rootfs：
```bash
root=/dev/mmcblk0p2
rootwait
rw
```

流程：

1. 解析 `root=`  
2. 找到 block device  
3. 讀取 super block  
4. mount 上 VFS：  
```text
mount_root()
→ vfs_kern_mount()
→ <fs_type>->mount()
```

### 5.1 initramfs 的位置

```text
initramfs 解壓
→ /init 執行
→ pivot_root 或 switch_root 到真實 rootfs
```

## 6. 常見檔案系統比較

| FS | 優點 | 缺點 | 用途 |
| --- | --- | --- | --- |
| **ext4** | 穩定、journaling | fragmentation | Linux rootfs |
| **f2fs** | 為 flash 設計 | 需較新 kernel | Android userdata |
| **squashfs** | 壓縮只讀 | 不可寫 | system.img / recovery |
| **erofs** | 高效 readonly | 只讀 | Android 13 A/B |
| **ubifs** | MTD 專用 | 不支援 block | NAND flash |
| **vfat** | 兼容性好 | 不支援權限 | boot partition |

工作中最常遇到：

- Yocto：ext4 + wic  
- Android：erofs / ext4 + super partition  
- Debian：ext4 rootfs  

## 7. block layer 與 buffer / bio

filesystem 不直接存取硬體，而透過 block layer：
```text
readpage()
→ submit_bio()
→ block driver (mmc, nvme, sd)
→ interrupt
→ page ready
```

### 7.1 bio 是 block I/O 的核心結構

```c
struct bio {
    struct bio_vec *bi_io_vec;
    sector_t bi_sector;
    ...
};
```

## 8. VFS 與裝置檔案

Linux 中一切皆檔案：

| 路徑 | 說明 |
|------|------|
| `/dev/mmcblk0` | MMC block |
| `/dev/sda` | SATA / USB / NVMe |
| `/dev/fb0` | framebuffer |
| `/dev/dri/card0` | DRM primary device |
| `/dev/rpmsg0` | RPMsg char device |

它們不是 regular file，而是：

-   character device
-   block device

VFS 仍透過 file_operations 管理它們。

## 9. 常見 Debug 方法

### 9.1 查看 mount

```bash
mount
cat /proc/mounts
```
查看 filesystem 資訊
```bash
dumpe2fs /dev/mmcblk0p2
tune2fs -l /dev/mmcblk0p2
```
查看 IO 狀況
```bash
iotop
iostat
```
追蹤 VFS 呼叫
```bash
trace-cmd record -e vfs
trace-cmd report
```
追蹤 page cache
```bash
cat /proc/meminfo
cat /proc/slabinfo
```

## 10. 常見問題與排查

| 問題 | 可能原因 | 解決方式 |
|------|-----------|-----------|
| rootfs 無法掛載 | fs type 錯 / superblock 損壞 | 使用 `fsck` / 重建 image |
| SD 卡讀取超慢 | card timing 配置錯誤 | DTS 調整 `bus-width` / UHS |
| Android system.img 掛載失敗 | erofs/squashfs format 錯 | 修正 `mkfs.erofs` / `mksquashfs` |
| mount: wrong fs type | module 缺 | Kernel config 加上：`CONFIG_EXT4_FS=y` |
| Boot 後掉到 initramfs | rootfs 分區代號錯 | 修正 `root=` 參數 |
