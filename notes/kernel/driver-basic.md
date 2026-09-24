# Linux Driver Basics

這份筆記整理 Linux kernel driver 開發的基礎概念與常見架構，適合作為 BSP / Android HAL 開發前的基礎。

## 1. 驅動程式的角色

- **目的**：在硬體與作業系統間建立抽象介面。
- **位置**：運行在 **kernel space**，提供 API 給 user space。
- **主要工作**：
  - 初始化/配置硬體 (probe/remove)。
  - 提供讀寫介面給 user space。
  - 處理中斷、DMA、電源管理。
  - 與 kernel 子系統 (net, input, drm, i2c, spi, etc.) 整合。

## 2. Driver 與 Kernel 架構

- **Application (user space)**  
  → system call (open, read, write, ioctl)  
  → **VFS / sysfs / ioctl handler**  
  → **Kernel driver**  
  → **硬體控制器 (I²C/SPI/UART/PCIe...)**

## 3. 常見驅動類型

1. **Character Driver**  
   - 提供 /dev/XXX 介面  
   - 適合簡單裝置（GPIO, LED, sensor）
2. **Block Driver**  
   - 提供儲存裝置介面（/dev/sda）  
   - 適合 disk/flash
3. **Network Driver**  
   - 提供 socket 介面  
   - WiFi / Ethernet driver 使用 net subsystem
4. **Misc / Platform Driver**  
   - 一般 SoC 裝置，多透過 **Device Tree** 定義  
   - 例如 I²C sensor, SPI 顯示器

## 4. 驅動程式的生命週期

### 4.1 模組入口/出口

```c
static int __init mydriver_init(void) {
    // 初始化
    return 0;
}

static void __exit mydriver_exit(void) {
    // 清理
}

module_init(mydriver_init);
module_exit(mydriver_exit);
```

### 4.2 Platform Driver 範例

```c
static int my_probe(struct platform_device *pdev) {
    // 硬體初始化
    return 0;
}

static int my_remove(struct platform_device *pdev) {
    // 資源釋放
    return 0;
}

static struct platform_driver my_driver = {
    .probe  = my_probe,
    .remove = my_remove,
    .driver = {
        .name = "my_device",
        .of_match_table = my_dt_ids,
    },
};

module_platform_driver(my_driver);
```

## 5. 與 User Space 溝通方式

- Character device node (/dev/xxx)
  - open/read/write/ioctl
- sysfs (/sys/class/...)
  - 提供簡單設定接口（echo 1 > /sys/...）
- debugfs (/sys/kernel/debug/...)
  - 偏向 debug 用
- netlink socket
  - kernel ↔ user space event 傳遞（如 WiFi 驅動）

## 6. Device Tree (DT) 在驅動中的角色

- 提供硬體描述，讓 driver 知道資源配置。

- 範例：
```c
my_device@0 {
    compatible = "vendor,mydevice";
    reg = <0x10>;
    interrupt-parent = <&gic>;
    interrupts = <5>;
};
```
- 驅動用 of_match_table 找到對應 node。

## 7. 常見 Debug 工具

- dmesg：driver log (printk)
- /sys/kernel/debug/：觀察 driver 狀態
- ftrace：function call trace
- kgdb：kernel debug
- perf：效能分析

## 8. 同步與鎖

- spinlock：短暫、不可睡眠
- mutex：可睡眠、長操作
- completion：等待事件完成
- atomic_t：原子操作

## 9. 模組資訊

```c
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Basic Driver Example");
```
