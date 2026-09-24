# libgpiod Userspace Flow 深入解析

## 1. 為什麼需要 libgpiod？

GPIO char device 提供的是：
```text
open()  
ioctl()  
read()
```
但直接使用 ioctl：

-   結構複雜

-   易錯

-   可讀性差

-   不利於快速 debug

**libgpiod 是官方 user space 封裝庫**

提供：

-   C API

-   CLI 工具

-   事件處理封裝

-   v2 API 支援

## 2. libgpiod 整體架構

```text
Application  
 │  
 ▼  
libgpiod  
 │  
 ▼  
ioctl (GPIO_V2_*)  
 │  
 ▼  
gpiolib (kernel)
```

## 3. 常用 CLI 工具

安裝：
```bash
sudo apt install gpiod
```
工具列表：

| 指令 | 功能說明 |  
|------------|------------------------------|  
| gpiodetect | 列出系統中的 gpiochip |  
| gpioinfo | 顯示 GPIO line 狀態與用途 |  
| gpioset | 設定 GPIO 輸出值 |  
| gpioget | 讀取 GPIO 輸入值 |  
| gpiomon | 監聽 GPIO edge 事件 |

## 4. BSP Bring-up 流程

### 4.1 Step 1 確認 controller

```bash
gpiodetect
```
輸出：
```text
gpiochip0 [rockchip-gpio] (32 lines)  
gpiochip1 [rockchip-gpio] (32 lines)
```

### 4.2 Step 2 查看 line 使用狀態

```bash
gpioinfo gpiochip0
```
會顯示：
```text
line 5: "reset" output active-low [used]
```
關鍵資訊：

-   name

-   direction

-   consumer

-   active-low

### 4.3 Step 3 手動控制 GPIO

```bash
gpioset gpiochip0 5=1
```
注意：

-   預設會持有 line

-   程式結束會釋放

### 4.4 Step 4 監聽中斷

```bash
gpiomon gpiochip0 12
```
當 edge 發生時：
```text
event: RISING EDGE
```

## 5. libgpiod C API Flow

### 5.1 取得 chip

```c
struct  gpiod_chip  *chip;  
chip  =  gpiod_chip_open("/dev/gpiochip0");
```

### 5.2 取得 line

```c
struct  gpiod_line  *line;  
line  =  gpiod_chip_get_line(chip, 5);
```

### 5.3 request output

```c
gpiod_line_request_output(line, "myapp", 1);
```

### 5.4 set value

```c
gpiod_line_set_value(line, 0);
```

### 5.5 event 監聽

```c
gpiod_line_request_rising_edge_events(line, "myapp");  
gpiod_line_event_wait(line, NULL);  
gpiod_line_event_read(line, &event);
```

## 6. Edge Event 完整流程

```text
Hardware interrupt  
 ↓  
gpio controller irq handler  
 ↓  
gpiolib irq domain  
 ↓  
wake up file descriptor  
 ↓  
poll()/epoll()  
 ↓  
gpiod_line_event_read()
```

## 7. 常見問題與排查（常見錯誤案例）

### 7.1 gpioset 無效果

可能原因：

-   pinctrl 沒 mux

-   line 被 kernel driver 使用

-   DT 設為 gpio-hog

-   active-low 設定誤判

### 7.2 gpiomon 沒事件

可能原因：

-   沒設定 edge

-   interrupt 沒 enable

-   pinctrl 沒設 input

-   IRQ mapping 有問題

## 8. Active-Low 問題

DT：
```dts
reset-gpios = <&gpio3 5 GPIO_ACTIVE_LOW>;
```
libgpiod 會顯示：
```text
active-low
```
代表：
```text
1 = low  
0 = high
```
很多 reset 拉不起來其實是搞錯這件事。

## 9. Multi-line Atomic Control

v2 支援：
```bash
gpioset gpiochip0 5=1 6=0
```
可以同時操作。

對於：

-   data bus

-   enable + reset sequence

-   LCD power sequence

非常重要。

## 10. 與 sysfs 差異

| 特性 | sysfs | libgpiod |  
|-------------|--------------|------------------|  
| ownership | 無 | 有（file descriptor 管理） |  
| edge event | 不穩定 | 正式支援 |  
| atomic | 不支援 | 支援 |  
| 未來狀態 | deprecated | 正式支援（推薦使用） |
