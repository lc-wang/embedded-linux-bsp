# Yocto Project：架構與 BSP 整合指南

本章整理 Yocto Project 的完整架構、build 原理、BSP 整合方式、配方編寫、image 建置流程，以及常見 debug 技巧。

## 1. Yocto 架構總覽

Yocto 不是 Linux 發行版，而是 **建置 Linux 發行版的框架**。

核心組成：

| 元件 | 說明 |
| --- | --- |
| **BitBake** | Yocto 的 build engine（負責解析 recipe、依賴、任務） |
| **Poky** | 官方 reference distribution（含 bitbake + default meta） |
| **OE-Core** | 基本 recipe + class（基礎 rootfs 建置框架） |
| **Meta Layers** | BSP / distro / application 的 recipes |
| **Recipes (.bb)** | 定義如何編譯 kernel、u-boot、apps |
| **Classes (.bbclass)** | 可重複利用的功能（如 autotools、systemd） |
| **Configuration (.conf)** | 設定 distro、machine、image type |

## 2. Yocto Build 流程

```text
bitbake <image>
↓
Parse recipes / classes / conf
↓
Resolve dependency graph
↓
Fetch (git / http / local)
↓
Patch / configure / compile
↓
Package (rpm/deb/ipk)
↓
Rootfs assemble
↓
Generate image (wic / ext4 / sdcard.img)
```

## 3. Meta Layer 架構

常見的 meta layers：
```text
meta/
meta-poky/
meta-openembedded/
meta-python/
meta-oe/
meta-virtualization/
meta-<vendor>/ ← vendor BSP: nxp, renesas, rockchip
meta-device/
meta-mylayer/ ← 你自己的 layer
```

### 3.1 建議你的專案層級

```text
meta-mycompany/
├── recipes-core/
├── recipes-kernel/
├── recipes-bsp/
├── recipes-connectivity/
├── recipes-security/
└── conf/layer.conf
```

## 4. Machine / Distro / Image 角色

Yocto 有三種 config 範疇：

### 4.1 MACHINE（硬體平台設定）

定義：
- kernel config
- dtb
- u-boot settings
- serial console
- storage layout

例：

MACHINE = "rzt2h-evk"
machine config：
```text
meta-renesas/conf/machine/rzt2h-evk.conf
```

### 4.2 DISTRO（發行版設定）

定義：
- systemd vs sysvinit
- security/policy (selinux)
- package format (rpm/ipk/deb)
- toolchain options

例：
```text
DISTRO = "poky" 或 "mydistro"
```

### 4.3 IMAGE（rootfs / OS 設定）

定義：
- 內建工具
- 檔案系統格式
- 是否加入 python / ssh / weston

例：
```bash
bitbake core-image-minimal
bitbake core-image-weston
bitbake my-image
```

## 5. Recipe 基本架構

範例：`hello.bb`

```bash
SUMMARY = "Hello World app"
LICENSE = "MIT"

SRC_URI = "file://hello.c"

do_compile() {
    ${CC} hello.c -o hello
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 hello ${D}${bindir}
}
```

### 5.1 常用變數

| 變數 | 說明 |
|------|------|
| `SRC_URI` | 原始碼來源 |
| `S` | 原始碼放置路徑 |
| `D` | install rootfs 暫存區 |
| `DEPENDS` | 編譯時依賴 |
| `RDEPENDS_${PN}` | rootfs 時依賴 |
| `${bindir}` | /usr/bin |
| `${sysconfdir}` |  |

## 6. Kernel / U-Boot 整合

### 6.1 Kernel Recipe

路徑：

```bash
recipes-kernel/linux/linux-renesas.bb
```
內容通常包含：
-   git repo
-   branch
-   defconfig
-   patches
-   device tree list

範例：

```bash
SRC_URI = "git://github.com/renesas/linux.git;branch=v6.1"
KERNEL_DEVICETREE = "r9a09g077.dtb"
```

### 6.2 U-Boot Recipe

```bash
recipes-bsp/u-boot/u-boot-renesas.bb
```
設定：

```bash
UBOOT_CONFIG = "rzt2h"
SRC_URI = "git://..."
```

## 7. RootFS 與 Image

Build 產出的檔案位於：

```bash
build/tmp/deploy/images/<machine>/
```

### 7.1 常見輸出

| 檔案 | 功能 |
|------|------|
| `.wic` | SD card image |
| `.ext4` | rootfs |
| `Image` | kernel image |
| `u-boot.bin` | bootloader |
| `*.dtb` | device tree |

## 8. Yocto Debug 技巧

### 8.1 查看 task log

```bash
bitbake -c menuconfig virtual/kernel
bitbake -c cleansstate <recipe>
bitbake -c do_compile -f <recipe>
```
log 位於：

```bash
build/tmp/work/<machine>/<recipe>/temp/log.do_compile
```

### 8.2 查看依賴圖

```bash
bitbake -g core-image-minimal
cat task-depends.dot
```

### 8.3 重新編譯某個 recipe

```bash
bitbake -c clean <recipe>
bitbake <recipe>
```

## 9. 常見問題與排查

| 問題 | 原因 | 修正方式 |
|------|-------|-----------|
| kernel recipe 不生效 | bbappend 未位於正確 layer | 放到 `recipes-kernel/linux/` |
| dtb 沒有加入 image | MACHINE config 沒有設定 | 加入 `KERNEL_DEVICETREE` |
| rootfs 編譯缺少檔案 | `DEPENDS` 未設定 | 在 recipe 補上 |
| build 過慢 | sstate-cache 未啟用 | 設定 `SSTATE_MIRRORS` |
| initramfs 無法 boot | rootfs type 錯誤 | 修正 `IMAGE_FSTYPES` |
