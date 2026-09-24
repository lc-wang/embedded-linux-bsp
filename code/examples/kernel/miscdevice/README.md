# miscdevice

Linux miscdevice 最小實作範例。

miscdevice 是 character device 的簡化封裝，
大量使用於：

- Bluetooth driver
- media / v4l2 輔助節點
- hwmon / sensor
- debug / control interface
- prototype driver

本章用來理解：

- miscdevice 是什麼
- misc_register() 做了哪些事
- 為什麼很多 driver 不自己處理 major/minor
- miscdevice 與 char_device 的差異

## 1. miscdevice 的設計理念

我要一個 /dev 節點
我不想管 major / minor
我只關心 file_operations

→ 用 miscdevice。

## 2. Kernel 原始碼對照
```text
drivers/char/misc.c
fs/char_dev.c
drivers/base/core.c
```

## 3. 使用流程總覽
```text
module_init()
└─ misc_register()
├─ alloc minor
├─ cdev_add()
├─ device_create()
└─ /dev/miscname
```

## 4. 與 char_device 的關係

| char_device | miscdevice |
|------------|-----------|
| alloc_chrdev_region | ✗ |
| cdev_init | ✗ |
| class_create | ✗ |
| device_create | ✗ |
| file_operations | ✓ |
| ioctl / read / write | ✓ |

miscdevice 幫你包掉前面一大段 boilerplate。
