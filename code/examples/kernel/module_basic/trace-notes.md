# Kernel trace 筆記 — module_basic

本文件說明當載入 kernel module 時，
實際進入 Linux kernel 原始碼的路徑位置。

## 1. Userspace 入口

執行指令：
```bash
insmod hello_module.ko
```

對應 syscall：
```text
finit_module(fd, "", 0)
```

## 2. Kernel 入口點

定義於：
```text
kernel/module/main.c
```

函式：
```text
SYSCALL_DEFINE3(finit_module)
```

## 3. 主要呼叫流程
```text
finit_module()
└─ idempotent_init_module()
   └─ init_module_from_file()
      ├─ kernel_read_file()          // 讀入 .ko
      └─ load_module()
         ├─ layout_and_allocate()
         ├─ simplify_symbols()       // 解析未定義符號
         ├─ apply_relocations()
         ├─ post_relocation()
         │  └─ module_finalize()     // arch-specific
         ├─ complete_formation()
         └─ do_init_module()
            ├─ do_one_initcall(mod->init)
            │  └─ hello_init()
            └─ module_enable_ro()    // v6.12 起為 module_enable_rodata_ro()
```

以 v6.6 / v6.12 的 `kernel/module/main.c` 為準。`init_module()` syscall（傳入 buffer 而非 fd）則走 `copy_module_from_user()` → `load_module()`。

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

```text
__initcall section
```
最終由：

```text
do_one_initcall()
```
統一執行。

## 5. 重要觀念

```text
module_init()
= driver 被載入

probe()
= device 被匹配
```
兩者意義完全不同。

## 6. 常用除錯指令

```bash
lsmod
cat /proc/modules
modinfo hello_module.ko
dmesg | tail
```

## 7. Trace 建議方式

### 7.1 function tracer

```bash
echo function > /sys/kernel/debug/tracing/current_tracer
echo do_init_module > /sys/kernel/debug/tracing/set_ftrace_filter
```
或使用：

```bash
trace-cmd record -p function do_init_module
```

## 8. 建議心智模型

```text
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
