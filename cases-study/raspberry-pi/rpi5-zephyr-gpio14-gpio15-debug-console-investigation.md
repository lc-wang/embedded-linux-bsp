# Raspberry Pi 5 Zephyr：GPIO14/GPIO15 Debug Console Bring-up Investigation

> 目標：嘗試將 Raspberry Pi 5 上的 Zephyr console 從官方預設的 Debug UART connector 改到 40-pin header 的 GPIO14/GPIO15，並記錄調查過程、實驗結果與最終結論。

本文聚焦在 **GPIO14/GPIO15 debug console bring-up**，不包含 Zephyr 安裝、SDK、west、SD card boot 等基礎流程。

----------

## 1. 背景

Raspberry Pi 5 在 Zephyr 官方 board support 中，可以透過 `rpi_5` target build 並從 SD card boot Zephyr。

基本 `blinky` sample 已驗證可成功執行：

```text
Raspberry Pi 5
  ↓
SD card boot
  ↓
zephyr.bin
  ↓
Zephyr runtime
  ↓
ACT LED blinking

```

但是 `samples/hello_world` 預設不會從 GPIO14/GPIO15 輸出 console。原因是 Zephyr `rpi_5` 預設 console 使用的是 Raspberry Pi 5 專用 **Debug UART connector**，而不是 40-pin header 上的 GPIO14/GPIO15。

本文記錄嘗試將 console 改到 GPIO14/GPIO15 的過程。

----------

## 2. 硬體與接線

GPIO14/GPIO15 是 Raspberry Pi 傳統 UART pins：

```text
Pi GPIO14 / Pin 8  / TXD -> USB-TTL RX
Pi GPIO15 / Pin 10 / RXD -> USB-TTL TX
Pi GND    / Pin 6        -> USB-TTL GND

```

注意：

```text
USB-TTL 必須是 3.3V TTL
不可使用 RS232 level
不可接 5V TX 到 Pi GPIO

```

Host 端使用：

```bash
minicom -D /dev/ttyUSB0 -b 115200

```

或：

```bash
picocom -b 115200 /dev/ttyUSB0

```

----------

## 3. Debian / Raspberry Pi OS 上 GPIO14/GPIO15 console 可正常使用

在 Raspberry Pi OS / Debian 中，GPIO14/GPIO15 可透過以下設定作為 serial console。

### 3.1 config.txt

```ini
[all]
enable_uart=1
dtoverlay=disable-bt

```

### 3.2 cmdline.txt

```text
console=serial0,115200 console=tty1 root=PARTUUID=45110d0a-02 rootfstype=ext4 fsck.repair=yes rootwait

```

### 3.3 Linux 中確認 UART device

在 Debian 中執行：

```bash
ls -l /dev/serial*
ls -l /dev/ttyAMA*
dmesg | grep -iE "ttyAMA|uart|serial"

```

觀察到：

```text
/dev/serial0 -> ttyAMA0
/dev/ttyAMA0
/dev/ttyAMA10

1f00030000.serial: ttyAMA0 at MMIO 0x1f00030000 (irq = 123) is a PL011 AXI
printk: console [ttyAMA0] enabled

```

這證明：

```text
GPIO14/GPIO15 接線 OK
USB-TTL OK
115200 console OK
Debian/Linux 可正常使用 GPIO14/GPIO15 當 console

```

----------

## 4. Linux live devicetree 確認 GPIO14/GPIO15 對應 RP1 UART0

在 Debian 中 dump live devicetree：

```bash
dtc -I fs -O dts /proc/device-tree > /tmp/rpi5-live.dts

```

查詢 serial alias：

```bash
grep -n -A30 -B10 "serial0" /tmp/rpi5-live.dts
grep -n -A50 -B10 "serial@30000" /tmp/rpi5-live.dts
grep -n -A30 -B10 "rp1_uart0_14_15" /tmp/rpi5-live.dts

```

重要 alias：

```dts
aliases {
        console = "/axi/pcie@1000120000/rp1/serial@30000";
        uart0   = "/axi/pcie@1000120000/rp1/serial@30000";
        serial0 = "/axi/pcie@1000120000/rp1/serial@30000";
        uart10  = "/soc@107c000000/serial@7d001000";
};

```

RP1 UART0 node：

```dts
serial@30000 {
        arm,primecell-periphid = <0x341011>;
        pinctrl-names = "default";
        pinctrl-0 = <0x2a>;
        clock-names = "uartclk", "apb_pclk";
        cts-event-workaround;
        interrupts = <0x19 0x04>;
        clocks = <0x02 0x0f 0x02 0x06>;
        skip-init;
        uart-has-rtscts;
        compatible = "arm,pl011-axi";
        status = "okay";
        reg = <0xc0 0x40030000 0x00 0x100>;
};

```

Symbol table 中也可看到：

```text
rp1_uart0       = /axi/pcie@1000120000/rp1/serial@30000
rp1_uart0_14_15 = /axi/pcie@1000120000/rp1/gpio@d0000/rp1_uart0_14_15

```

因此 Debian/Linux 的 GPIO14/GPIO15 console 不是 BCM2712 內部 UART，而是：

```text
GPIO14/GPIO15
  ↓
RP1 pinctrl
  ↓
RP1 UART0
  ↓
/axi/pcie@1000120000/rp1/serial@30000
  ↓
Linux ttyAMA0

```

----------

## 5. Zephyr rpi_5 預設 console 是 uart10

在 Zephyr source 中查詢 Raspberry Pi 5 UART node：

```bash
grep -R "uart[0-9]*:" -n dts/arm64/broadcom boards/raspberrypi/rpi_5 | head -80
grep -R "serial@" -n dts/arm64/broadcom boards/raspberrypi/rpi_5 | head -80

```

觀察到：

```text
dts/arm64/broadcom/bcm2712.dtsi:78: uart10: serial@107d001000

```

也就是 Zephyr Pi 5 DTS 目前只有 `uart10`，對應 Pi 5 專用 debug UART connector。

Zephyr rpi_5 board DTS 中沒有 Linux live DT 裡的：

```text
/axi/pcie@1000120000/rp1/serial@30000

```

因此，Linux 的：

```ini
enable_uart=1
dtoverlay=disable-bt

```

以及：

```text
console=serial0,115200

```

不會自動讓 Zephyr console 改到 GPIO14/GPIO15。

----------

## 6. 嘗試一：用 Zephyr overlay 新增 RP1 UART0 node

建立 overlay：

```bash
mkdir -p app-overlays
nano app-overlays/rpi5-rp1-uart0.overlay

```

加入一個實驗用 RP1 UART0 node：

```dts
/ {
	chosen {
		zephyr,console = &rp1_uart0;
		zephyr,shell-uart = &rp1_uart0;
	};

	rp1_uart0_clk: rp1-uart0-clock {
		compatible = "fixed-clock";
		clock-frequency = <48000000>;
		#clock-cells = <0>;
	};

	rp1_uart0: serial@1f00030000 {
		compatible = "arm,pl011";
		reg = <0x1f 0x00030000 0x100>;
		interrupts = <0 123 4 0>;
		clocks = <&rp1_uart0_clk>;
		current-speed = <115200>;
		status = "okay";
	};
};

```

Build：

```bash
west build -p always -b rpi_5 samples/hello_world \
  -DDTC_OVERLAY_FILE=$PWD/app-overlays/rpi5-rp1-uart0.overlay

```

檢查 generated DTS：

```bash
grep -n "zephyr,console" build/zephyr/zephyr.dts
grep -n -A30 "serial@1f00030000" build/zephyr/zephyr.dts

```

確認 console 已改到 RP1 UART0：

```dts
zephyr,console = &rp1_uart0;

rp1_uart0: serial@1f00030000 {
        compatible = "arm,pl011";
        reg = <0x1f 0x30000 0x100>;
        interrupts = <0x0 0x7b 0x4 0x0>;
        clocks = <&rp1_uart0_clk>;
        current-speed = <0x1c200>;
        status = "okay";
};

```

複製 `zephyr.bin` 到 SD card boot partition 後測試：

```bash
sudo cp build/zephyr/zephyr.bin /mnt/SDK1/
sync

```

結果：

```text
GPIO14/GPIO15 仍無 console output

```

----------

## 7. DTS / overlay troubleshooting

### 7.1 overlay path 問題

錯誤：

```text
fatal error: app-overlays/rpi5-gpio-uart.overlay: No such file or directory

```

原因：`DTC_OVERLAY_FILE` 的相對路徑解析可能不是目前 shell 目錄。

修正：使用 absolute path 或 `$PWD`：

```bash
west build -p always -b rpi_5 samples/hello_world \
  -DDTC_OVERLAY_FILE=$PWD/app-overlays/rpi5-rp1-uart0.overlay

```

----------

### 7.2 top-level node 語法錯誤

錯誤：

```text
parse error: expected label reference (&foo)

```

錯誤寫法：

```dts
rp1_uart0: serial@1f00030000 {
        ...
};

```

修正：新增 node 要放在 `/ { ... };` 裡：

```dts
/ {
        rp1_uart0: serial@1f00030000 {
                ...
        };
};

```

----------

### 7.3 reg cells 數量錯誤

錯誤：

```text
'reg' property ... has length 16, which is not evenly divisible by 12
#address-cells = 2, #size-cells = 1

```

錯誤寫法：

```dts
reg = <0x1f 0x00030000 0x0 0x1000>;

```

修正：

```dts
reg = <0x1f 0x00030000 0x1000>;

```

後續依 Linux UART register size 改成：

```dts
reg = <0x1f 0x00030000 0x100>;

```

----------

### 7.4 interrupts cells 數量錯誤

錯誤：

```text
'interrupts' property ... has length 12, which is not evenly divisible by 16
#interrupt-cells = 4

```

錯誤寫法：

```dts
interrupts = <0 123 4>;

```

修正：

```dts
interrupts = <0 123 4 0>;

```

----------

### 7.5 Linux-only DTS properties 不被 Zephyr binding 接受

嘗試照抄 Linux node 時，以下 properties 不被 Zephyr `arm,pl011.yaml` 接受：

```dts
arm,primecell-periphid = <0x341011>;
cts-event-workaround;
skip-init;
uart-has-rtscts;

```

錯誤範例：

```text
'arm,primecell-periphid' appears ... but is not declared in 'properties:' in dts/bindings/serial/arm,pl011.yaml

```

```text
'cts-event-workaround' appears ... but is not declared in 'properties:' in dts/bindings/serial/arm,pl011.yaml

```

結論：Linux DTS properties 不能直接照搬到 Zephyr overlay，必須符合 Zephyr binding。

----------

## 8. 嘗試二：blinky + RP1 UART0 overlay

為了確認加入 RP1 UART0 node 後是否導致 Zephyr crash，改 build `blinky` 並套用同一份 overlay：

```bash
west build -p always -b rpi_5 samples/basic/blinky \
  -DDTC_OVERLAY_FILE=$PWD/app-overlays/rpi5-rp1-uart0.overlay

sudo cp build/zephyr/zephyr.bin /mnt/SDK1/
sync

```

結果：

```text
LED 仍會閃爍

```

代表：

```text
Zephyr 沒有因為 RP1 UART0 node crash
加入 RP1 UART0 node 不會讓系統直接 data abort
問題不是 Zephyr boot flow

```

----------

## 9. 嘗試三：在 Zephyr app 中手動設定 GPIO14/15 pinmux

因為 Linux 使用 `rp1_uart0_14_15` pinctrl group，所以嘗試在 Zephyr app 中直接設定 GPIO14/15 function。

建立測試 app：

```bash
mkdir -p myapps/rpi5_uart_gpio/src

```

### 9.1 CMakeLists.txt

`myapps/rpi5_uart_gpio/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rpi5_uart_gpio)

target_sources(app PRIVATE src/main.c)

```

### 9.2 prj.conf

`myapps/rpi5_uart_gpio/prj.conf`：

```conf
CONFIG_PRINTK=n
CONFIG_SERIAL=n
CONFIG_UART_CONSOLE=n

```

### 9.3 GPIO14/15 pinmux test

```c
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <stdint.h>

#define RP1_IO_BANK0_BASE        0x1f000d0000UL
#define RP1_GPIO14_CTRL          (RP1_IO_BANK0_BASE + 0x074)
#define RP1_GPIO15_CTRL          (RP1_IO_BANK0_BASE + 0x07c)

#define RP1_GPIO_FUNC_MASK       0x1fU
#define RP1_GPIO_FUNC_UART0      4U

static void rp1_gpio_set_uart0(void)
{
	uint32_t v;

	v = sys_read32(RP1_GPIO14_CTRL);
	v &= ~RP1_GPIO_FUNC_MASK;
	v |= RP1_GPIO_FUNC_UART0;
	sys_write32(v, RP1_GPIO14_CTRL);

	v = sys_read32(RP1_GPIO15_CTRL);
	v &= ~RP1_GPIO_FUNC_MASK;
	v |= RP1_GPIO_FUNC_UART0;
	sys_write32(v, RP1_GPIO15_CTRL);
}

```

結果：

```text
仍無 console output

```

----------

## 10. 嘗試四：不使用 Zephyr UART driver，直接 raw write PL011

為了排除 Zephyr PL011 driver / console path 問題，改成直接寫 RP1 UART0 PL011 registers。

RP1 UART0 Linux MMIO：

```text
0x1f00030000

```

Raw write 測試：

```c
#define RP1_UART0_BASE           0x1f00030000UL
#define UART_DR                  (RP1_UART0_BASE + 0x00)
#define UART_FR                  (RP1_UART0_BASE + 0x18)
#define UART_IBRD                (RP1_UART0_BASE + 0x24)
#define UART_FBRD                (RP1_UART0_BASE + 0x28)
#define UART_LCRH                (RP1_UART0_BASE + 0x2c)
#define UART_CR                  (RP1_UART0_BASE + 0x30)
#define UART_ICR                 (RP1_UART0_BASE + 0x44)

#define UART_FR_TXFF             BIT(5)

```

一開始假設 UART clock 為 48 MHz：

```text
IBRD = 26
FBRD = 3

```

結果：

```text
仍無 console output

```

----------

## 11. Debian 中讀出 Linux 成功時的 UART / GPIO register

回 Debian，在 GPIO14/GPIO15 console 正常時讀 register：

```bash
sudo busybox devmem 0x1f00030030 32   # UART_CR
sudo busybox devmem 0x1f00030024 32   # UART_IBRD
sudo busybox devmem 0x1f00030028 32   # UART_FBRD
sudo busybox devmem 0x1f0003002c 32   # UART_LCRH
sudo busybox devmem 0x1f00030018 32   # UART_FR

sudo busybox devmem 0x1f000d0074 32   # GPIO14_CTRL
sudo busybox devmem 0x1f000d007c 32   # GPIO15_CTRL

```

Linux 成功時結果：

```text
UART_CR    = 0x00000F01
UART_IBRD  = 0x0000001B
UART_FBRD  = 0x00000008
UART_LCRH  = 0x00000070
UART_FR    = 0x00000197

GPIO14_CTRL = 0x00000084
GPIO15_CTRL = 0x00000084

```

觀察：

```text
GPIO14/15 不是單純 FUNCSEL = 4，而是完整值 0x84
UART divisor 是 IBRD=27, FBRD=8
這比較像 UART clock 是 50 MHz，而不是 48 MHz

```

----------

## 12. 嘗試五：Zephyr raw write 模仿 Linux register 值

將 Zephyr raw write 測試改成完全模仿 Linux 成功時的 register 值：

```c
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <stdint.h>

#define RP1_IO_BANK0_BASE        0x1f000d0000UL
#define RP1_GPIO14_CTRL          (RP1_IO_BANK0_BASE + 0x074)
#define RP1_GPIO15_CTRL          (RP1_IO_BANK0_BASE + 0x07c)

#define RP1_UART0_BASE           0x1f00030000UL
#define UART_DR                  (RP1_UART0_BASE + 0x00)
#define UART_FR                  (RP1_UART0_BASE + 0x18)
#define UART_IBRD                (RP1_UART0_BASE + 0x24)
#define UART_FBRD                (RP1_UART0_BASE + 0x28)
#define UART_LCRH                (RP1_UART0_BASE + 0x2c)
#define UART_CR                  (RP1_UART0_BASE + 0x30)
#define UART_ICR                 (RP1_UART0_BASE + 0x44)

#define UART_FR_TXFF             BIT(5)

static void rp1_gpio_set_uart0_like_linux(void)
{
	sys_write32(0x00000084, RP1_GPIO14_CTRL);
	sys_write32(0x00000084, RP1_GPIO15_CTRL);
}

static void rp1_uart0_init_like_linux(void)
{
	sys_write32(0x00000000, UART_CR);
	sys_write32(0x000007ff, UART_ICR);
	sys_write32(0x0000001b, UART_IBRD);
	sys_write32(0x00000008, UART_FBRD);
	sys_write32(0x00000070, UART_LCRH);
	sys_write32(0x00000f01, UART_CR);
}

static void rp1_uart0_putc(char c)
{
	while (sys_read32(UART_FR) & UART_FR_TXFF) {
	}

	sys_write32((uint32_t)c, UART_DR);
}

static void rp1_uart0_puts(const char *s)
{
	while (*s) {
		if (*s == '
') {
			rp1_uart0_putc('
');
		}
		rp1_uart0_putc(*s++);
	}
}

int main(void)
{
	rp1_gpio_set_uart0_like_linux();
	rp1_uart0_init_like_linux();

	while (1) {
		rp1_uart0_puts("RAW RP1 UART0 GPIO14/15 like Linux registers
");
		k_sleep(K_MSEC(1000));
	}

	return 0;
}

```

Build 時不使用 overlay，避免 Zephyr serial driver 先初始化 UART：

```bash
west build -p always -b rpi_5 myapps/rpi5_uart_gpio
sudo cp build/zephyr/zephyr.bin /mnt/SDK1/
sync

```

結果：

```text
仍無 console output

```

----------

## 13. 最終結論

經過以上測試，可以確認：

```text
1. Raspberry Pi 5 可以從 SD card boot Zephyr
2. Zephyr blinky 可正常執行
3. Zephyr overlay 可以把 console 指到手動加入的 RP1 UART0 node
4. 加入 RP1 UART0 node 不會讓 Zephyr crash
5. 直接 raw write RP1 UART0 PL011 registers 仍無輸出
6. 模仿 Linux 成功時 UART/GPIO register 值仍無輸出

```

因此問題不在：

```text
不是單純 config.txt 問題
不是單純 Zephyr chosen console 問題
不是單純 PL011 reg base 問題
不是單純 baud divisor 問題
不是單純 GPIO14/15 CTRL 值問題

```

真正問題應該是：

```text
Zephyr 目前沒有完整初始化 RP1。
GPIO14/GPIO15 位於 RP1 I/O controller 後方，
而 Linux 使用 RP1 clock controller + RP1 pinctrl + RP1 UART0 driver 共同完成 console。

Zephyr 目前 rpi_5 board support 僅支援 BCM2712 uart10，
並未完整支援 RP1 UART0 / pinctrl / clock / reset。

```

若要真正支援 GPIO14/GPIO15 作為 Zephyr console，需要補齊：

```text
1. RP1 PCIe / bus address translation model
2. RP1 clock controller driver
3. RP1 reset / power handling
4. RP1 pinctrl driver
5. RP1 UART0 devicetree node
6. rpi_5 board DTS 將 zephyr,console 指到 RP1 UART0

```
