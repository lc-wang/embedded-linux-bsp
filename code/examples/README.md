# Synchronization Examples

這裡收錄幾個常見 Linux Kernel 同步機制的練習範例：

| 範例檔案 | 機制 | 說明 |
|-----------|------|------|
| `spinlock_example.c` | Spinlock | 適用於中斷或短暫臨界區，不可睡眠 |
| `mutex_example.c` | Mutex | 適用於 process context，可睡眠 |
| `completion_example.c` | Completion | 等待事件完成的同步方法 |

## 1. 編譯與測試
```bash
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
sudo insmod spinlock_example.ko
dmesg | tail
sudo rmmod spinlock_example
```
