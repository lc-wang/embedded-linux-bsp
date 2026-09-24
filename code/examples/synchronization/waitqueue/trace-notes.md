# Kernel trace notes — waitqueue

## 1. Level 1

想像：

```
沒有資料 → 先睡
有資料 → 被叫醒
這就是 waitqueue。
```

## 2. Level 2
```
Thread A (read):  
 wait_event()  
 ↓  
條件不成立 → 睡眠  
  ```
  ```
Thread B (write):  
 更新條件  
 ↓  
wake_up()  
 ↓  
Thread A 被喚醒
```

## 3. Level 3
```
wait_event()  
 └─ prepare_to_wait()  
 └─ schedule()  
```
```
wake_up()  
 └─ try_to_wake_up()
```

## 4. 與 completion 的差異

| 項目       | completion   | waitqueue        |
|------------|--------------|------------------|
| 類型       | 單次事件     | 條件判斷         |
| 重複使用   | 不適合       | 適合             |
| 使用方式   | complete()   | wake_up()        |
| 判斷條件   | 隱含         | 明確條件         |

## 5. wait_event 的本質
```
wait_event(wq, condition)
```
等價於：
```
while (!condition)  
 sleep
```

## 6. 常見問題與排查

✗ 忘記檢查 condition  
✗ wake_up() 但條件沒改  
✗ race condition（未加鎖）

## 7. 心智模型

waitqueue = 睡到 condition 成立
