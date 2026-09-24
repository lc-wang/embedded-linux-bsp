# Kernel trace notes — completion

## 1. Level 1

想像兩個 thread：

```text
Thread A：等事情完成
Thread B：事情完成後通知
completion 就是：
```
A: 等  
B: 完成後叫醒 A

## 2. Level 2
```text
Thread A:  
 wait_for_completion()  
  
 ↓（睡眠）  
  
Thread B:  
 complete()  
  
 ↓  
  
喚醒 Thread A
```

## 3. Level 3
```text
wait_for_completion()  
 └─ wait_for_common()  
 └─ schedule()  
```
```text
complete()  
 └─ wake_up_process()
```

## 4. 與 mutex 的差異

| 項目 | mutex | completion |  
|----------|------------------|-------------------|  
| 用途 | 保護資源 | 等待事件 |  
| 行為 | lock / unlock | wait / complete |  
| 使用情境 | critical section | async event |

## 5. 為什麼 driver 很常用？

因為 driver 很多是：
```text
start hardware  
 ↓  
等 interrupt  
 ↓  
完成
```
這時候：
```text
wait_for_completion()  
+ complete()
```
是最自然的寫法。

## 6. 心智模型

completion 是「同步兩個時間點」  
不是「保護資料」
