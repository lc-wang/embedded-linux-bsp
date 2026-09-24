# Kernel trace notes — miscdevice

## 1. misc_register() 做了什麼？

位置：
```
drivers/char/misc.c
```

主要流程：
```
misc_register()
├─ ida_alloc() // 分配 minor
├─ cdev_init()
├─ cdev_add()
├─ device_create()
└─ 建立 /dev/mymisc
```

## 2. open() trace
```
open("/dev/mymisc")
└─ chrdev_open()
└─ file->f_op = my_fops
└─ my_open()
```

和 char_device 完全一樣。

## 3. 為什麼 Bluetooth 很愛用 miscdevice？

因為：

- 只需要 control path
- 不想管理 major/minor
- 不需要複雜 sysfs
- 快速建立 /dev 節點

實例：
```
drivers/bluetooth/
drivers/media/
drivers/hwmon/
```

## 4. 使用時機建議

### 4.1 適合 miscdevice

- debug interface
- control ioctl
- prototype driver
- 單一節點裝置

### 4.2 不適合 miscdevice

- 需要多個 minor
- 高度結構化 sysfs
- 真正的 data plane 裝置
