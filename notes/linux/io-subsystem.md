# Linux I/O Subsystem 全解析

本章介紹 Linux block I/O 子系統，包括：

- VFS → Page Cache → BIO → Block Layer → Request Queue → Driver → Device
- blk-mq（Multi-Queue Block Layer）
- I/O scheduler（noop / deadline / mq-deadline / bfq）
- 文件系統與 I/O 行為關聯
- read/write path 全流程
- eMMC、NVMe、UFS 等裝置差異
- 常見效能瓶頸與 debug 技巧

## 1. I/O Path 總覽

完整資料流程如下：
```text
User Space
↓
VFS (read/write)
↓
Page Cache / Writeback
↓
BIO (Block I/O)
↓
Block Layer / blk-mq
↓
Request Queue (RQ)
↓
Block Driver (eMMC/NVMe/UFS)
↓
Hardware Command Queue
↓
Storage Device
```
分成三層：

- **上層：VFS / Page Cache**
- **中層：Block Layer（BIO / blk-mq / rq）**
- **底層：Driver / HW**

## 2. Page Cache 與 Writeback

### 2.1 Page Cache

Linux 將檔案讀取緩存在 Page Cache：
```text
read() → 若 cache 命中，直接回應 → 不會進入 storage
```
可用：
```bash
cat /proc/meminfo | grep -i cache
```

### 2.2 Writeback（Dirty pages）

寫入行為分成兩種：

| 寫方式 | 說明 |
|--------|------|
| Buffered Write | 先寫入 page cache → 稍後 writeback |
| Direct I/O | 直接寫入 block device，不經 page cache |

Dirty page 清理：
```bash
/proc/sys/vm/dirty_ratio
/proc/sys/vm/dirty_background_ratio
```

## 3. BIO（Block I/O）

當資料需要真正落盤，就會建立一個 BIO：
```c
struct bio {
struct bio_vec *bi_io_vec; // segments
sector_t bi_sector; // 起始 sector
}
```

功能：

- 將資料分段（segment）
- 傳遞 I/O 請求到 block layer
- 透過 merge/coalesce 來減少命令量

## 4. Block Layer（blk-mq）

現代 Linux 使用 **blk-mq（multi-queue）架構**。

傳統單一 queue 已被淘汰：

- 無法利用多核心
- lock contention 高

blk-mq 支援：

- 每 CPU 一個 SW queue
- 每 device 多個 HW queue

流程：
```text
BIO
↓
blk-mq → choose SW queue
↓
merge / split
↓
dispatch to HW queue
↓
driver send command
```

## 5. Request Queue（RQ）

每個 block device（例如 `/dev/mmcblk0`, `/dev/nvme0n1`）都有一個 request queue。

RQ 是 driver 啟動 I/O 的唯一入口。

查看 queue：
```bash
ls /sys/block/mmcblk0/queue
```
常見屬性：

| 屬性 | 說明 |
|------|------|
| nr_requests | queue 深度 |
| scheduler | 使用的 I/O scheduler |
| rq_affinity | 綁定 CPU |
| logical_block_size | 裝置區塊大小 |
| max_sectors_kb | 單次 I/O 限制 |

## 6. I/O Scheduler（排程器）

功用：

- 決定 READ / WRITE 的服務順序
- 減少 seek time（對 HDD 特別重要）
- 對 mobile 平台可改善延遲

### 6.1 noop（適合快閃裝置）

不排序、不合併 → 直接丟給 driver

eMMC / UFS / NVMe 常用。

### 6.2 deadline / mq-deadline

確保：

- Read latency 優先  
- Write 避免飢餓  

適合 Android 開機加速。

### 6.3 bfq（Budget Fair Queueing）

適合：

- 互動式裝置（Android/手機）
- 多應用同時讀寫

## 7. Storage Device 差異

| 裝置 | 特點 | queue |
|------|------|-------|
| eMMC | 單 queue，速度中等 | 單 HW 佇列 |
| UFS | 多 queue，快 | 多 HW 佇列 |
| NVMe | 支援 64k 隊列 | 高效能、多核 |
| SD | 最慢 | 單 queue |

Android 通常：

- `/data` → f2fs + UFS  
- `/system` → erofs / ext4（唯讀）  

## 8. Read Path（流程）

```text
read()
↓
Page Cache hit → 回傳
↓
Page fault → 建立 BIO
↓
blk-mq submit_bio()
↓
SW queue → HW queue
↓
Driver（mmc/nvme/ufs）
↓
Device DMA 回傳資料
↓
Page Cache → User Space
```

## 9. Write Path（流程）

```text
write()
↓
寫入 Page Cache → Dirty
↓
Dirty 過多 → writeback
↓
BIO → Block Layer
↓
Rq queue → Driver
↓
DMA → Device
```
Write 常被延遲因為：

- Dirty page throttle  
- Writeback congestion  
- device 不支援 parallel write  

## 10. I/O Latency 與 Performance Debug

### 10.1 查看 I/O 統計

```bash
iostat -x 1
```
檢查：

- r/s, w/s  
- await（重要！）  
- svctm  

### 10.2 查看 block device 隊列

```bash
cat /sys/block/mmcblk0/queue/nr_requests
```

### 10.3 Android Trace（systrace）

看 block I/O 延遲：
```bash
block:block_rq_issue
block:block_rq_complete
```

### 10.4 使用 ftrace

```bash
echo 1 > /sys/kernel/debug/tracing/events/block/enable
cat trace
```

### 10.5 Page Cache 檢查

```bash
cat /proc/meminfo | grep Dirty
```

## 11. 常見問題與排查（常見效能問題與解法）

| 問題 | 根因 | 解法 |
|------|-------|------|
| 開機時間慢 | random read latency 高 | 用 erofs + preload |
| 應用卡頓 | writeback 擋住 foreground thread | 調整 dirty_ratio |
| eMMC busy | 單 queue 無法並行 | queue depth 調整 |
| NVMe 飽和 | irq affinity 不佳 | 調整 affinity /
| Read latency 偶爾飆高 | GC（UFS/eMMC） | io_is_busy 設定 |
