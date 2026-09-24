# char_device

Linux character device（字元裝置）最小實作範例。

本章節用來建立以下核心觀念：

- /dev/xxx 是怎麼出現的
- file_operations 是什麼
- open / read / write / ioctl 的實際呼叫流程
- userspace 如何進入 kernel driver

此範例示範：

- register_chrdev()
- cdev_init() / cdev_add()
- struct file_operations
- device_create()
- /dev 節點建立流程

這是以下 subsystem 的共同基礎：

- Bluetooth HCI UART
- Wi-Fi driver control path
- DRM ioctl
- V4L2
- miscdevice
- remoteproc character interface

## 1. Kernel 原始碼對照
```
fs/char_dev.c
fs/open.c
fs/read_write.c
drivers/base/core.c
```

## 2. 完整資料流
```
userspace
└─ open("/dev/mychardev")
└─ sys_openat()
└─ do_sys_open()
└─ chrdev_open()
└─ file->f_op = fops
└─ fops->open()

read()
└─ vfs_read()
└─ file->f_op->read()

write()
└─ vfs_write()
└─ file->f_op->write()

ioctl()
└─ do_vfs_ioctl()
└─ file->f_op->unlocked_ioctl()
```

## 3. 非常重要的觀念

/dev/xxx
不是 driver

而是：
inode → file → file_operations

driver 真正的入口是：

struct file_operations
