
# Raspberry Pi 5 安裝與執行 Zephyr：Get / Build / Install / Troubleshooting

> 目標：在 Raspberry Pi 5 上透過 microSD card 開機執行 Zephyr RTOS，並記錄從環境建立、build、安裝到 troubleshooting 的完整流程。

本文以 BSP / Embedded Linux 工程師視角整理，重點不是 Zephyr API 教學，而是如何把 Zephyr 在 Raspberry Pi 5 上實際跑起來。

----------

## 1. 測試環境

### Host

```text
Host OS      : Ubuntu 22.04
Python       : Python 3.12.x
Zephyr       : 4.4.99 development tree
Zephyr SDK   : 1.0.0-rc1
Board        : Raspberry Pi 5
Board target : rpi_5 / bcm2712
Boot media   : microSD card

```

### Workspace path example

本文範例使用以下 workspace：

```bash
~/zephyr

```

目錄結構完成後大致如下：

```text
zephyr/
├── .venv/
├── zephyr/
├── modules/
├── tools/
├── bootloader/
├── zephyr-sdk-1.0.0-rc1/
└── zephyr-sdk-1.0.0-rc1_linux-x86_64_gnu.tar.xz

```

----------

## 2. 安裝 Host 端相依套件

```bash
sudo apt update
sudo apt install --no-install-recommends \
  git cmake ninja-build gperf ccache dfu-util device-tree-compiler \
  wget python3-dev python3-venv python3-tk xz-utils file make gcc \
  gcc-multilib g++-multilib libsdl2-dev libmagic1

```

如果是 Ubuntu 22.04，系統預設 Python 通常是 3.10，但目前較新的 Zephyr tree 需要 Python 3.12。

可先檢查：

```bash
python3 --version
python3.12 --version

```

如果沒有 `python3.12`，可安裝 Python 3.12。Ubuntu 22.04 可能需要額外來源，例如 deadsnakes PPA：

```bash
sudo apt install software-properties-common
sudo add-apt-repository ppa:deadsnakes/ppa
sudo apt update
sudo apt install python3.12 python3.12-venv python3.12-dev

```

----------

## 3. 建立 Python venv 與安裝 west

建議使用 Python virtual environment，避免污染系統 Python，也避免和 Yocto / Buildroot / Android BSP 工具鏈互相影響。

```bash
mkdir -p ~/zephyr
cd ~/zephyr

python3.12 -m venv .venv
source .venv/bin/activate

python --version
pip install --upgrade pip
pip install west

```

確認：

```bash
which python
python --version
which west
west --version

```

預期 Python 版本要是 3.12 以上：

```text
Python 3.12.x

```

----------

## 4. 取得 Zephyr source tree

Zephyr 不建議只用 `git clone` 主 repo，因為它需要許多 modules。建議使用 `west init` / `west update`。

```bash
cd ~/zephyr

west init .
west update
west zephyr-export

```

完成後，安裝 Zephyr Python requirements。

如果目前在 workspace 根目錄：

```bash
pip install -r zephyr/scripts/requirements.txt

```

如果目前已經在 Zephyr repo 內：

```bash
cd zephyr
pip install -r scripts/requirements.txt

```

----------

## 5. 安裝 Zephyr SDK

### 5.1 SDK 版本選擇

本次使用的 Zephyr tree 為：

```text
Zephyr version: 4.4.99

```

此版本需要新版 Zephyr SDK。若使用 `zephyr-sdk-0.17.4`，會遇到 SDK 不相容問題。因此使用：

```text
zephyr-sdk-1.0.0-rc1

```

### 5.2 下載與安裝 SDK

```bash
cd ~/zephyr

wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.0-rc1/zephyr-sdk-1.0.0-rc1_linux-x86_64_gnu.tar.xz

tar xvf zephyr-sdk-1.0.0-rc1_linux-x86_64_gnu.tar.xz
cd zephyr-sdk-1.0.0-rc1
./setup.sh

```

安裝過程中可選：

```text
Install host tools [y/n]? y
Register Zephyr SDK CMake package [y/n]? y

```

成功訊息會類似：

```text
Registering Zephyr SDK CMake package ...
Zephyr-sdk (.../zephyr-sdk-1.0.0-rc1/cmake)
has been added to the user package registry in:
~/.cmake/packages/Zephyr-sdk

All done.

```

### 5.3 安裝 OpenOCD udev rules

```bash
cd ~/zephyr

sudo cp ./zephyr-sdk-1.0.0-rc1/hosttools/sysroots/x86_64-pokysdk-linux/usr/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload

```

> Note: SDK 0.17.x 的路徑是 `sysroots/...`，SDK 1.0.x 的 host tools 路徑可能是 `hosttools/sysroots/...`。實際請以解壓後目錄為準。

----------

## 6. Build Raspberry Pi 5 hello_world

進入 Zephyr repo：

```bash
cd ~/zephyr/zephyr

```

build hello_world：

```bash
west build -p always -b rpi_5 samples/hello_world

```

成功時會看到：

```text
-- Board: rpi_5, qualifiers: bcm2712
-- Found host-tools: zephyr 1.0.0 (.../zephyr-sdk-1.0.0-rc1)
-- Found toolchain: zephyr 1.0.0 (.../zephyr-sdk-1.0.0-rc1)
...
[136/136] Linking C executable zephyr/zephyr.elf
Generating files from .../build/zephyr/zephyr.elf for board: rpi_5/bcm2712

```

主要產物：

```bash
ls -lh build/zephyr/zephyr.*

```

常用檔案：

```text
build/zephyr/zephyr.elf
build/zephyr/zephyr.bin
build/zephyr/zephyr.dts
build/zephyr/.config

```

Raspberry Pi 5 firmware 開機時會載入：

```text
zephyr.bin

```

----------

## 7. 安裝到 microSD boot partition

如果 microSD 已經安裝 Raspberry Pi OS / Debian，通常不需要重新格式化。可以直接使用既有 FAT32 boot partition。

Raspberry Pi OS / Debian 的 microSD 常見分割如下：

```text
/dev/sdX1  FAT32 boot partition
/dev/sdX2  Linux rootfs ext4

```

### 7.1 掛載 boot partition

先確認裝置名稱：

```bash
lsblk

```

假設 boot partition 已掛載到：

```text
/mnt/SDK1

```

或手動掛載：

```bash
mkdir -p /tmp/rpi-boot
sudo mount /dev/sdX1 /tmp/rpi-boot

```

本文以下以 `/mnt/SDK1` 為例。

### 7.2 備份原本 Raspberry Pi OS / Debian boot config

```bash
mkdir -p ~/zephyr/backup

cp ~/zephyr/backup/config.txt
cp ~/zephyr/backup/cmdline.txt

```

### 7.3 複製 Zephyr binary

```bash
cd ~/zephyr/zephyr
sudo cp build/zephyr/zephyr.bin /mnt/SDK1/
sync

```

### 7.4 Zephyr boot 用 config.txt

把 boot partition 的 `config.txt` 改成 Zephyr 專用：

```bash
sudo tee /mnt/SDK1/config.txt > /dev/null <<'EOF'
kernel=zephyr.bin
enable_uart=1
uart_2ndstage=1
EOF
sync

```

最小檔案需求：

```text
/mnt/SDK1/config.txt
/mnt/SDK1/zephyr.bin
/mnt/SDK1/bcm2712-rpi-5-b.dtb

```

檢查：

```bash
ls -lh /mnt/SDK1/zephyr.bin
ls -lh /mnt/SDK1/bcm2712-rpi-5-b.dtb
cat /mnt/SDK1/config.txt

```

> `cmdline.txt` 主要是 Linux kernel 使用。Zephyr boot 時主要由 Raspberry Pi firmware 根據 `config.txt` 載入 `zephyr.bin`。

----------

## 8. 執行 blinky 驗證 Zephyr boot flow

由於 Raspberry Pi 5 的 Zephyr console 預設不是 GPIO14/GPIO15，而是 Pi 5 專用 debug UART connector，因此若沒有 Debug Probe / JST connector，建議先用 `blinky` 驗證 Zephyr 是否成功開機。

```bash
cd ~/zephyr/zephyr

west build -p always -b rpi_5 samples/basic/blinky
sudo cp build/zephyr/zephyr.bin /mnt/SDK1/
sync

```

將 microSD 插回 Raspberry Pi 5 開機。

如果板上 LED 有閃爍，代表：

```text
Zephyr build OK
SD card boot OK
Raspberry Pi firmware 成功載入 zephyr.bin
Zephyr runtime 成功執行

```

----------

## 9. Serial console 注意事項

### 9.1 Raspberry Pi OS / Debian 的 GPIO14/GPIO15 console

在 Debian / Raspberry Pi OS 中，GPIO14/GPIO15 可透過以下設定作為 serial console：

`config.txt`：

```ini
enable_uart=1
dtoverlay=disable-bt

```

`cmdline.txt`：

```text
console=serial0,115200 console=tty1 root=PARTUUID=45110d0a-02 rootfstype=ext4 fsck.repair=yes rootwait

```

接線：

```text
Pi GPIO14 / Pin 8  / TXD -> USB-TTL RX
Pi GPIO15 / Pin 10 / RXD -> USB-TTL TX
Pi GND    / Pin 6        -> USB-TTL GND

```

注意 USB-TTL 必須是 3.3V TTL。

### 9.2 Zephyr rpi_5 預設 console 不在 GPIO14/GPIO15

目前 Zephyr `rpi_5` board DTS 中，console 預設使用：

```dts
zephyr,console = &uart10;
zephyr,shell-uart = &uart10;

```

而 `uart10` 對應 Raspberry Pi 5 專用 debug UART connector，不是 40-pin header 的 GPIO14/GPIO15。

目前在 Zephyr source 中可看到：

```bash
grep -R "uart[0-9]*:" -n dts/arm64/broadcom boards/raspberrypi/rpi_5 | head -80
grep -R "serial@" -n dts/arm64/broadcom boards/raspberrypi/rpi_5 | head -80

```

結果顯示 Pi 5 / BCM2712 目前只定義了：

```text
dts/arm64/broadcom/bcm2712.dtsi:78: uart10: serial@107d001000

```

而 `uart0` 是 BCM2711，也就是 Raspberry Pi 4 相關 DTS 中的 node：

```text
dts/arm64/broadcom/bcm2711.dtsi:101: uart0: uart@fe201000

```

因此以下 overlay 對 Pi 5 不適用：

```dts
/ {
	chosen {
		zephyr,console = &uart0;
		zephyr,shell-uart = &uart0;
	};
};

&uart0 {
	status = "okay";
	current-speed = <115200>;
};

```

原因是 Zephyr Pi 5 DTS 裡沒有 `uart0` label。

### 9.3 為什麼 Debian 可用 GPIO14/GPIO15，但 Zephyr 不行？

```text
Debian / Linux:
  config.txt / overlay
    ↓
  firmware 修改 Linux DTB
    ↓
  Linux kernel 看到 serial0
    ↓
  Linux serial driver 綁到 GPIO14/15 UART
    ↓
  console=serial0,115200 生效

Zephyr:
  config.txt 載入 zephyr.bin
    ↓
  Zephyr 使用 build-time 產生的 zephyr.dts
    ↓
  目前 rpi_5 DTS 只有 uart10
    ↓
  hello_world printk 輸出到 uart10

```

所以 `enable_uart=1` 與 `dtoverlay=disable-bt` 能讓 Debian/Linux 使用 GPIO14/GPIO15，但不會自動讓 Zephyr console 改到 GPIO14/GPIO15。

----------

## 10. Troubleshooting

### 10.1 `pip install -r zephyr/scripts/requirements.txt` 找不到檔案

錯誤：

```text
ERROR: Could not open requirements file: [Errno 2] No such file or directory: 'zephyr/scripts/requirements.txt'

```

原因：目前已經在 Zephyr repo 裡：

```text
.../zephyr/zephyr

```

卻執行：

```bash
pip install -r zephyr/scripts/requirements.txt

```

導致實際尋找：

```text
.../zephyr/zephyr/zephyr/scripts/requirements.txt

```

修正：

若在 workspace 根目錄：

```bash
pip install -r zephyr/scripts/requirements.txt

```

若在 Zephyr repo 裡：

```bash
pip install -r scripts/requirements.txt

```

----------

### 10.2 Python version too old

錯誤：

```text
Could NOT find Python3: Found unsuitable version "3.10.12", but required is at least "3.12"

```

原因：目前 Zephyr tree 要求 Python >= 3.12，但 venv 是用 Python 3.10 建立。

修正：刪掉舊 venv，使用 Python 3.12 重建。

```bash
cd ~/zephyr

deactivate 2>/dev/null || true
rm -rf .venv

python3.12 -m venv .venv
source .venv/bin/activate

python --version
pip install --upgrade pip
pip install west
pip install -r zephyr/scripts/requirements.txt

```

----------

### 10.3 Could not find Zephyr-sdk

錯誤：

```text
ZEPHYR_TOOLCHAIN_VARIANT not set, trying to locate Zephyr SDK
Could not find a package configuration file provided by "Zephyr-sdk"

```

原因：尚未安裝 Zephyr SDK，或 SDK 沒有註冊到 CMake package registry。

修正：下載並執行 SDK 的 `setup.sh`：

```bash
cd ~/zephyr
cd zephyr-sdk-1.0.0-rc1
./setup.sh

```

選擇：

```text
Install host tools [y/n]? y
Register Zephyr SDK CMake package [y/n]? y

```

----------

### 10.4 Zephyr SDK version incompatible

錯誤現象：

```text
Could not find a configuration file for package "Zephyr-sdk" that is compatible

```

已安裝：

```text
zephyr-sdk-0.17.4

```

目前 Zephyr：

```text
Zephyr version: 4.4.99

```

原因：Zephyr tree 太新，SDK 0.17.4 不相容。

修正：移除舊 CMake registry，改安裝 SDK 1.0.x。

```bash
rm -rf ~/.cmake/packages/Zephyr-sdk

cd ~/zephyr
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.0-rc1/zephyr-sdk-1.0.0-rc1_linux-x86_64_gnu.tar.xz

tar xvf zephyr-sdk-1.0.0-rc1_linux-x86_64_gnu.tar.xz
cd zephyr-sdk-1.0.0-rc1
./setup.sh

```

----------

### 10.5 Devicetree overlay file exists but build says No such file

錯誤：

```text
fatal error: app-overlays/rpi5-gpio-uart.overlay: No such file or directory

```

但檔案確實存在：

```bash
ls app-overlays/
rpi5-gpio-uart.overlay

```

原因：`DTC_OVERLAY_FILE` 的相對路徑解析不一定相對於目前 shell 所在目錄，有可能被 application / build context 影響。

修正：使用 absolute path 或 `$PWD`。

```bash
west build -p always -b rpi_5 samples/hello_world \
  -DDTC_OVERLAY_FILE=$PWD/app-overlays/rpi5-gpio-uart.overlay

```

或：

```bash
west build -p always -b rpi_5 samples/hello_world \
  -DDTC_OVERLAY_FILE=~/zephyr/zephyr/app-overlays/rpi5-gpio-uart.overlay

```

----------

### 10.6 GPIO14/GPIO15 沒有 hello_world console output

現象：

```text
hello_world build OK
zephyr.bin 已複製到 boot partition
Pi 5 開機後 GPIO14/GPIO15 serial console 沒有訊息

```

已確認 `config.txt`：

```ini
kernel=zephyr.bin
enable_uart=1
uart_2ndstage=1

```

原因：Zephyr `rpi_5` 預設 console 是 `uart10`，不是 GPIO14/GPIO15。

檢查：

```bash
grep -n "zephyr,console" build/zephyr/zephyr.dts

```

可能看到類似：

```dts
zephyr,console = &uart10;

```

目前解法：

```text
1. 使用 Pi 5 專用 Debug UART connector / Debug Probe
2. 或先使用 blinky 驗證 Zephyr boot
3. 若要使用 GPIO14/GPIO15，需要補 RP1 UART0 的 Zephyr devicetree / driver support

```

----------

## 11. 切回 Raspberry Pi OS / Debian

如果要從 Zephyr 切回原本 Debian，只要還原 boot partition 的 `config.txt` / `cmdline.txt`。

```bash
sudo cp ~/zephyr/backup/config.txt /mnt/SDK1/config.txt
sudo cp ~/zephyr/backup/cmdline.txt /mnt/SDK1/cmdline.txt
sync

```

原本 Debian 可使用 GPIO14/GPIO15 debug console 的設定範例：

`config.txt`：

```ini
[all]
enable_uart=1
dtoverlay=disable-bt

```

`cmdline.txt`：

```text
console=serial0,115200 console=tty1 root=PARTUUID=45110d0a-02 rootfstype=ext4 fsck.repair=yes rootwait

```

----------

## 12. 目前結論

本次已完成：

```text
✅ Zephyr source tree 取得
✅ Python 3.12 venv 建立
✅ west 安裝
✅ Zephyr SDK 1.0.0-rc1 安裝
✅ rpi_5 hello_world build 成功
✅ zephyr.bin 複製到 Raspberry Pi boot partition
✅ Raspberry Pi 5 從 SD card boot Zephyr 成功
✅ blinky sample 驗證 Zephyr runtime 成功執行

```

目前限制：

```text
❌ GPIO14/GPIO15 尚未能作為 Zephyr console

```

原因：

```text
Raspberry Pi 5 的 GPIO14/GPIO15 屬於 RP1 I/O controller UART0，
但目前 Zephyr rpi_5 board DTS 只定義 BCM2712 的 uart10，
也就是 Pi 5 專用 debug UART connector。

```

----------

## 13. 常用指令摘要

### Activate venv

```bash
cd ~/zephyr
source .venv/bin/activate

```

### Build hello_world

```bash
cd ~/zephyr/zephyr
west build -p always -b rpi_5 samples/hello_world

```

### Build blinky

```bash
cd ~/zephyr/zephyr
west build -p always -b rpi_5 samples/basic/blinky

```

### Install zephyr.bin to SD boot partition

```bash
sudo cp build/zephyr/zephyr.bin /mnt/SDK1/
sync

```

### Zephyr config.txt

```bash
sudo tee /mnt/SDK1/config.txt > /dev/null <<'EOF'
kernel=zephyr.bin
enable_uart=1
uart_2ndstage=1
EOF
sync

```

### Check generated DTS

```bash
grep -n "zephyr,console" build/zephyr/zephyr.dts
grep -n "uart" build/zephyr/zephyr.dts | head -80

```

### Check Raspberry Pi 5 UART labels in Zephyr source

```bash
grep -R "uart[0-9]*:" -n dts/arm64/broadcom boards/raspberrypi/rpi_5 | head -80
grep -R "serial@" -n dts/arm64/broadcom boards/raspberrypi/rpi_5 | head -80

```
