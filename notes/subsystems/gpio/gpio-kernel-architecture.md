
# GPIO Kernel Architecture 深入解析

----------

# 1. GPIO 在 Linux Kernel 中的定位

GPIO（General Purpose Input Output）是：

> **最基礎的硬體控制介面**  
> 幾乎所有 SoC 都依賴 GPIO 來控制 reset / power / interrupt / enable / mux

在 BSP / Kernel bring-up 裡：

-   Panel reset
    
-   PMIC enable
    
-   WiFi power
    
-   Touch IRQ
    
-   Backlight enable
    
-   PCIe reset
    
-   USB hub reset
    

幾乎全部都會用到 GPIO

----------

# 2. GPIO 在 Kernel 的整體架構
```
User space
    │
    │  (libgpiod / ioctl)
    ▼
GPIO Character Device (/dev/gpiochipX)
    │
    ▼
gpiolib core
    │
    ▼
GPIO controller driver (SoC driver)
    │
    ▼
Hardware register
```
----------

# 3. gpiolib 的角色

核心檔案：

`drivers/gpio/gpiolib.c` 

負責：

-   管理所有 gpio controller
    
-   管理 gpio descriptor
    
-   提供 API 給 driver
    
-   建立 character device
    
-   轉換 DT phandle → gpio
    

----------

# 4. GPIO Controller Driver

每個 SoC 都有自己的 driver，例如：

-   Rockchip → gpio-rockchip.c
    
-   NXP i.MX → gpio-mxc.c
    
-   Renesas → gpio-rcar.c
    

他們會註冊：

`struct  gpio_chip` 

核心成員：
```
struct gpio_chip {
    const char *label;
    int base;
    int ngpio;

    int (*direction_input)(...);
    int (*direction_output)(...);
    int (*get)(...);
    void (*set)(...);
};
```
這就是 GPIO 的最底層硬體抽象層

----------

# 5. GPIO Descriptor 機制

舊 API：
```
gpio_request()
gpio_direction_output()
gpio_set_value()
```
新 API：
```
gpiod_get()
gpiod_direction_output()
gpiod_set_value()
```
核心概念：

> descriptor-based GPIO model

好處：

-   支援 device tree
    
-   支援 ACPI
    
-   不依賴 global number
    
-   更安全
    

----------

# 6. GPIO Numbering 問題

早期是 global number：

`GPIO = bank * 32 + offset` 

問題：

-   DT overlay 可能改變 base
    
-   不同 SoC 會不同
    

所以現在：

**不要使用 GPIO number**  
使用 descriptor + DT label

----------

# 7. 與 Pin Controller 的關係

GPIO ≠ pinctrl

-   GPIO 是電氣方向與電平控制
    
-   pinctrl 是功能 mux / bias / drive strength
    

流程：
```
pinctrl 先 mux 成 gpio
 ↓
gpio driver 才能控制方向
```
很多人 debug GPIO 問題時忽略這一層。

----------

# 8. Kernel 初始化流程

boot 時：
```
pinctrl init
   ↓
gpio controller probe
   ↓
register gpiochip
   ↓
建立 /dev/gpiochipX
```
你可以透過：
```
ls /sys/class/gpio
ls /dev/gpiochip*
```
----------

# 9. GPIO 在 Driver 中的典型用法
```
struct gpio_desc *reset_gpio;

reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
gpiod_set_value(reset_gpio, 0);
```
DT：

`reset-gpios = <&gpio3 5 GPIO_ACTIVE_LOW>;` 

----------

# 10. 常見錯誤觀念

| 錯誤觀念                          | 正確理解                              |
|-----------------------------------|----------------------------------------|
| 只要 GPIO 設為 output 就一定會動  | 可能 pinctrl 尚未正確 mux 到 GPIO 模式 |
| sysfs 可控制所有 GPIO             | sysfs 已 deprecated，應使用 character device |
| GPIO number 永遠固定              | global number 可能因 kernel / DTS 改變 |

