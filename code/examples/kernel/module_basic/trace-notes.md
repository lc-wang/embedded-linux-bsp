# Kernel trace 筆記 — module_basic

本文件說明當載入 kernel module 時，
實際進入 Linux kernel 原始碼的路徑位置。

## 1. Userspace 入口

執行指令：
```
insmod hello_module.ko
```

對應 syscall：
```
finit_module(fd, "", 0)
```

## 2. Kernel 入口點

定義於：
```
kernel/module/main.c
```

函式：
```
SYSCALL_DEFINE3(finit_module)
```

## 3. 主要呼叫流程
```
finit_module()
└─ load_module()
├─ layout_and_allocate()
├─ copy_module_from_user()
├─ simplify_symbols()
├─ resolve_symbols()
├─ module_finalize()
└─ do_init_module()
└─ do_one_initcall()
└─ hello_init()
```

## 4. 為什麼所有 driver 都長一樣？

因為：

```c
module_init(driver_init);
```
實際會展開為：

```c
__initcall(driver_init);
```
所有 driver 的 init function 都會被放進：

```
__initcall section
```
最終由：

```
do_one_initcall()
```
統一執行。

## 5. 重要觀念

```
module_init()
= driver 被載入

probe()
= device 被匹配
```
兩者意義完全不同。

## 6. 常用除錯指令

```
lsmod
cat /proc/modules
modinfo hello_module.ko
dmesg | tail
```

## 7. Trace 建議方式

### 7.1 function tracer

```
echo function > /sys/kernel/debug/tracing/current_tracer
echo do_init_module > /sys/kernel/debug/tracing/set_ftrace_filter
```
或使用：

```
trace-cmd record -p function do_init_module
```

## 8. 建議心智模型

```
insmod
  ↓
module_init()
  ↓
driver register
  ↓
bus match
  ↓
probe()
```
不要把 module_init() 當成 probe()。
