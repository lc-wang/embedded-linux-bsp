# gpiolib 與 GPIO Character Device 深入解析

## 1. 為什麼 sysfs 被淘汰？

舊機制：
```
/sys/class/gpio/export
/sys/class/gpio/gpioX/direction
/sys/class/gpio/gpioX/value
```
問題：

-   race condition 嚴重

-   無法 atomic control

-   無法安全管理 ownership

-   無法支援 edge event 高效率通知

-   不適合多程序

因此從 Linux 4.8 開始：

> **GPIO character device 成為正式介面**

## 2. GPIO Character Device 架構

建立位置：

```text
/dev/gpiochip0
/dev/gpiochip1
...
```

每一個 gpio controller 對應一個 gpiochip

### 2.1 核心流程

```
SoC gpio driver
      ↓
gpiochip_add_data()
      ↓
gpiolib 註冊 character device
      ↓
/dev/gpiochipX 建立
```

## 3. gpiolib 內部結構

核心檔案：
```
drivers/gpio/gpiolib.c
drivers/gpio/gpiolib-cdev.c
```
重要結構：
```
struct gpio_chip
struct gpio_device
struct gpio_desc
```

### 3.1 gpio_device

代表一個 gpio controller instance：
```
struct gpio_device {
    struct gpio_chip *chip;
    struct cdev chrdev;
    dev_t devt;
};
```
這就是 char device 的根源

## 4. file_operations

gpiolib 會註冊：
```
static const struct file_operations gpio_fileops = {
    .owner = THIS_MODULE,
    .open = gpio_chrdev_open,
    .release = gpio_chrdev_release,
    .unlocked_ioctl = gpio_chrdev_ioctl,
};
```
所有 user space 操作最後都會進入：

`gpio_chrdev_ioctl()` 

## 5. IOCTL 架構

主要命令：

### 5.1 v1 API（舊）

```
GPIO_GET_LINEHANDLE_IOCTL
GPIO_GET_LINEEVENT_IOCTL
```

### 5.2 v2 API（推薦）

```
GPIO_V2_GET_LINE_IOCTL
GPIO_V2_LINE_SET_VALUES_IOCTL
GPIO_V2_LINE_GET_VALUES_IOCTL
```

## 6. 使用流程

### 6.1 Step 1 開啟 gpiochip

`fd = open("/dev/gpiochip0", O_RDONLY);` 

### 6.2 Step 2 取得 line handle

```
struct gpio_v2_line_request req;
ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req);
```

### 6.3 Step 3 設定方向

在 request 中指定：

`GPIO_V2_LINE_FLAG_OUTPUT` 

### 6.4 Step 4 設定電平

```
struct gpio_v2_line_values vals;
ioctl(line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
```

## 7. 為什麼 v2 API 更好？

v2 改善：

-   支援 multi-line atomic operation

-   支援 bias (pull-up/down)

-   支援 drive type (open drain)

-   支援 event configuration

-   更清楚 ownership

對於：

-   interrupt

-   reset sequence

-   多 GPIO 同時切換

非常重要。

## 8. Edge Event 機制

如果設定：
```
GPIO_V2_LINE_FLAG_EDGE_RISING
GPIO_V2_LINE_FLAG_EDGE_FALLING
```
Kernel 會：
```
interrupt
   ↓
gpio_irq_handler
   ↓
wake up file descriptor
   ↓
user space read()
```
這就是 libgpiod 的 event 模型

## 9. 與 Descriptor API 的關係

Kernel driver 使用：

`gpiod_get()` 

User space 使用：

`/dev/gpiochipX` 

兩者都共用：

`gpio_desc` 

差別在 ownership model。

## 10. 常見問題與排查（BSP bring-up 會遇到的問題）

### 10.1 情境 1：reset pin 拉不起來

可能：

-   pinctrl 沒 mux

-   regulator 沒開

-   gpio 被 kernel driver 佔用

### 10.2 情境 2：user space 設定失敗

可能：

-   權限問題

-   line 已被 kernel request

-   DT 設為 hog

## 11. gpio hog

DT 可設定：
```
gpio-hog;
output-high;
```
此時：

> user space 不能 request 該 GPIO

常見於：

-   panel enable

-   power rail

## 12. Debug 建議

```
ls -l /dev/gpiochip*
```
```
gpioinfo
```
`cat /sys/kernel/debug/gpio`
