# Kernel trace notes — char_device

## 1. /dev 節點從哪來？
```text
device_create()
└─ drivers/base/core.c
└─ device_add()
└─ uevent
└─ udev 建立 /dev/mychardev
```

## 2. open() trace
```text
open("/dev/mychardev")
└─ sys_openat
└─ do_sys_open
└─ do_dentry_open
└─ chrdev_open
└─ file->f_op = my_fops
└─ my_open()
```

## 3. read() trace
```text
read()
└─ vfs_read()
└─ file->f_op->read()
└─ my_read()
```

## 4. write() trace
```text
write()
└─ vfs_write()
└─ file->f_op->write()
└─ my_write()
```

## 5. ioctl() trace
```text
ioctl()
└─ do_vfs_ioctl()
└─ file->f_op->unlocked_ioctl()
└─ my_ioctl()
```

## 6. 最重要心智模型
```text
userspace
↓
syscall
↓
VFS
↓
file_operations
↓
driver
```

## 7. 常見問題與排查（常見錯誤觀念）

✗ /dev 是 driver  
✗ open() 直接進 driver  

✓ 實際是：

inode → struct file → f_op

driver 只是 callback 集合。

## 8. 為什麼這一章超重要？

因為：

- Bluetooth HCI → ioctl
- Wi-Fi cfg80211 → ioctl
- DRM → ioctl
- V4L2 → ioctl
- media → ioctl

**90% kernel driver 的 userspace 入口都在這裡。**
