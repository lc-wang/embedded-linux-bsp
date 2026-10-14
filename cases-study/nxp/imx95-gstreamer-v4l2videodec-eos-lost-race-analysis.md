# i.MX95 GStreamer v4l2videodec 提早 EOS 遺失導致 pipeline 卡住分析（Wave6 VPU 解碼逾時）

## 1. 問題概述

一片 i.MX95 SMARC 自製板在 burn-in 測試中，VPU 硬體解碼測項偶爾逾時。測項每一輪用 `gst-launch-1.0` 解一段 10 秒的測試影片，只取前 250 個 frame：

```bash
gst-launch-1.0 -q filesrc location=bbb_480p60_h264_baseline_10s.mp4 ! qtdemux ! h264parse ! \
    v4l2h264dec ! fakesink sync=false num-buffers=250
```

正常每次約 1 秒結束，但偶爾 `gst-launch-1.0` 不會退出，直到測項的 60 秒 `timeout` 把它砍掉，測項判為 FAIL。

已知的現象：

- 全負載 burn-in 下約 2 ~ 3.5% 的解碼會逾時（114 次中 4 次、175 次中 2 次）。
- 只跑 VPU 解碼、沒有其他負載時不會發生（數百次皆正常）。
- 逾時當下 `dmesg` 沒有任何 wave6 / v4l2 錯誤，VPU firmware log 也是空的。
- 用同一版 BSP 長時間播放影片，不一定能重現。

一開始的判斷是「VPU 在高負載下卡死」，懷疑是 VPU driver 或 firmware 的問題。實際分析後，VPU 和 kernel driver 都沒有問題：**frame 早就解完了，卡住的是 GStreamer `v4l2videodec` 在提早 EOS 時的一個 race condition**。

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95 |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP LF_6.12.34_2.1.0（Yocto 5.2，fsl-imx-wayland） |
| Kernel | 6.12.34（linux-imx lts-next） |
| VPU | Chips&Media Wave6，decoder `/dev/video9`，firmware v4.0.0 |
| GStreamer | 1.26.0 |
| gst-plugins-good | `1.26.0.imx`（nxp-imx/gst-plugins-good 分支 `MM_04.10.01_2508_L6.12.34`，commit `6b8d873`） |
| 測試影片 | 720x480 60fps H.264 baseline，10 秒共 600 frames |
| 負載工具 | `stress-ng --cpu 6 --vm 2 --vm-bytes 2G --io 2`（6 核 Cortex-A55） |

## 3. 除錯過程

### 3.1 先區分「被拖慢」還是「真的卡死」

光看「60 秒逾時」分不出是負載太重導致解碼變慢，還是 VPU 停止工作。所以把測法改成可以觀察內部狀態的 probe（完整腳本見附錄 A）：

- 每次解碼的上限從 60 秒放寬到 300 秒。如果只是變慢，應該會在 300 秒內解完。
- 每次解碼前後記錄 `/proc/interrupts` 裡 `vpu_irq` 的總數。正常解 250 frames 會產生 252 ~ 253 次中斷。
- 超過 60 秒時，每 10 秒記錄一次 VPU 中斷數和 gst 的 CPU 時間（`/proc/<pid>/stat` 的 utime + stime），並抓所有 thread 的 kernel stack（`/proc/<pid>/task/*/stack`）、`dmesg` 和 wave6 的 firmware log。

判斷方式：

| 觀察 | 代表 |
|---|---|
| 最後有解完，中斷數一直增加 | 只是負載太重，解碼變慢 |
| 中斷數停住，thread 停在 v4l2 / wave6 的 kernel 函式 | VPU 或 driver 卡死 |
| 中斷數停住，thread 都在 userspace 等待 | 卡在 userspace（GStreamer） |

### 3.2 分階段實驗

| 階段 | 背景負載 | frames | 解碼次數 | 卡住 |
|---|---|---|---|---|
| A | 無 | 250 | 513 | 0 |
| B | stress-ng | 250 | 133 | 1 |
| C | 其餘 burn-in 測項（31 項） | 250 | 177 | 0 |
| D | stress-ng | 600（完整解完） | 336 | 0 |
| E | stress-ng | 250 | 332 | 0 |
| B（第二輪，20 分鐘） | stress-ng | 250 | 293 | 3 |

幾個重點：

- 只用 stress-ng 加負載就能重現，不需要完整的 burn-in。
- 負載更重的 C 階段（load average 約 35）反而沒有卡住，所以不是「負載越重越卡」，比較像機率很低、跟時序有關的 race。
- 完整解完 600 frames 的 D 階段 336 次都沒卡住。

注意：第一次跑 C 階段時，背景負載包含 NPU 測項，結果觸發了另一個 NPU firmware 載入時的 SError kernel panic。那是不相關的獨立問題，之後的實驗都把 NPU 從負載中拿掉。

### 3.3 卡住當下的狀態

B 階段第一次卡住（第 118 次解碼）時，probe 記錄到：

```text
06:41:41 irq=164097 cpu=32
06:41:52 irq=164097 cpu=32
06:42:02 irq=164097 cpu=32
06:42:12 irq=164097 cpu=32
06:42:22 irq=164097 cpu=32
06:42:32 irq=164097 cpu=32
```

- 這次解碼在卡住前已經產生 254 次 VPU 中斷，**250 個 frame 都已經解完了**。
- 之後 60 秒中斷數和 CPU 時間完全不變，程式在睡覺，不是因為負載太重而沒被排到。
- 所有 thread 的 kernel stack 都停在 `do_sys_poll` 或 `futex_wait_queue`，沒有任何 thread 停在 v4l2 / wave6 的函式裡。
- 到 300 秒被強制結束，下一次解碼馬上就正常了（1.07 秒）。

所以 VPU 沒有卡住，卡住的是 userspace。

### 3.4 用 gdb 看 userspace

在 probe 裡加上 gdb，超過 60 秒時抓所有 thread 的 backtrace：

```bash
gdb -p $pid -batch -ex "thread apply all bt" > gdb_bt
```

結果整理（板子上沒有 GStreamer 的 debug symbols，部分 frame 只有位址）：

| Thread | 狀態 |
|---|---|
| `gst-launch-1.0` 主執行緒 | `g_main_loop_run` → `ppoll`，在等 bus 上的訊息（EOS） |
| `qtdemux0:sink` | `libgstreamer` 內 → `g_cond_wait`，GstTask 已暫停 |
| `v4l2h264dec0:src` | 同一個位址 → `g_cond_wait`，GstTask 已暫停 |
| `gmain`、`pool-spawner` | 閒置 |

所有傳資料的 streaming thread 都停了，主程式在等一個永遠不會來的 EOS。看起來 EOS 在某個 element 裡不見了，但沒有 symbols，無法確定是哪一步。

### 3.5 用 GST_DEBUG 比對正常與卡住的 log

每次解碼都開 GST_DEBUG 寫到 tmpfs，正常結束就刪掉，只保留卡住那次和一次正常的作對照：

```bash
GST_DEBUG="v4l2videodec:6,v4l2bufferpool:5,videodecoder:6,basesink:5,fakesink:5,qtdemux:4,GST_EVENT:5,GST_PADS:4,task:5,GST_STATES:4"
GST_DEBUG_FILE=/tmp/gst_$n.log
```

開了 debug log 之後仍然會重現（314 次中卡住 4 次）。兩份 log 前半段完全一樣：

```text
fakesink   gst_fake_sink_render:<fakesink0> we are EOS
v4l2videodec gst_v4l2_video_dec_loop:<v4l2h264dec0> Leaving output thread: eos
v4l2bufferpool gst_v4l2_buffer_pool_flush_start:<v4l2h264dec0:pool0:sink> start flushing
v4l2bufferpool gst_v4l2_buffer_pool_dqbuf:<v4l2h264dec0:pool0:sink> poll error flushing
videodecoder gst_video_decoder_drop_frame:<v4l2h264dec0> dropping frame 0:00:04.166666666
```

接下來就分岔了。正常（第 6 次解碼）：

```text
0:00:02.003450005 task gst_task_set_state_unlocked:<v4l2h264dec0:src> Changing task to state paused
0:00:02.004452026 videodecoder gst_video_decoder_decode_frame:<v4l2h264dec0> flow error eos
0:00:02.011797987 task gst_task_set_state_unlocked:<qtdemux0:sink> Changing task to state paused
0:00:02.011926896 GST_EVENT gst_event_new_custom: creating new event eos
0:00:02.012764052 basesink gst_base_sink_default_event:<fakesink0> Now posting EOS
```

卡住（第 164 次解碼）：

```text
0:00:02.505547190 videodecoder gst_video_decoder_decode_frame:<v4l2h264dec0> flow error flushing
0:00:02.505668100 task gst_task_set_state_unlocked:<qtdemux0:sink> Changing task to state paused
0:00:02.505984330 task gst_task_set_state_unlocked:<v4l2h264dec0:src> Changing task to state paused
```

差別在於：

| | 正常 | 卡住 |
|---|---|---|
| 解碼器輸出 task 變成 paused 的時間 | .003450 | .505984 |
| 輸入端 `decode_frame` 回傳的時間 | .004452（在 paused 之後） | .505547（在 paused 之前） |
| 回傳值 | `eos` | `flushing` |
| qtdemux 的反應 | 送出 EOS 事件，fakesink 發出 EOS 訊息 | 當作 flushing，直接暫停，不送 EOS |

`GST_FLOW_FLUSHING` 對 qtdemux 來說是「pipeline 正在 flush 或關閉」，不需要送 EOS。所以 EOS 就這樣被吃掉了。

### 3.6 對照原始碼

以 nxp-imx/gst-plugins-good 分支 `MM_04.10.01_2508_L6.12.34` 的 `sys/v4l2/gstv4l2videodec.c` 為準。

輸出 thread（`gst_v4l2_video_dec_loop`）離開時：

```c
  GST_DEBUG_OBJECT (decoder, "Leaving output thread: %s",
      gst_flow_get_name (ret));

  gst_buffer_replace (&buffer, NULL);
  self->output_flow = ret;                    /* ① 記下離開原因（EOS） */
  gst_v4l2_object_unlock (self->v4l2output);  /* ② 叫醒輸入 thread */
  gst_pad_pause_task (decoder->srcpad);       /* ③ task 狀態變成 PAUSED */
}
```

輸入 thread（`gst_v4l2_video_dec_handle_frame`）送 frame 拿到 FLUSHING 時：

```c
    if (ret == GST_FLOW_FLUSHING) {
      if (gst_pad_get_task_state (GST_VIDEO_DECODER_SRC_PAD (self)) !=
          GST_TASK_STARTED)
        ret = self->output_flow;
      goto drop;
    }
```

呼叫關係：

```text
qtdemux0:sink thread
└─ gst_video_decoder_chain()
   └─ gst_video_decoder_decode_frame()
      └─ gst_v4l2_video_dec_handle_frame()
         └─ gst_v4l2_buffer_pool_process()   ← 被 ② 中斷，回傳 FLUSHING
            → 檢查 srcpad task 狀態 → 決定回傳 FLUSHING 或 output_flow

v4l2h264dec0:src thread
└─ gst_v4l2_video_dec_loop()
   └─ gst_video_decoder_finish_frame() → fakesink 回傳 EOS
      → ① output_flow = EOS → ② unlock(v4l2output) → ③ pause_task()
```

## 4. Root Cause 分析

### 4.1 Race condition 的時序

```text
輸出 thread（v4l2h264dec0:src）        輸入 thread（qtdemux0:sink）
───────────────────────────────        ───────────────────────────────
fakesink 收滿 250 frames，回傳 EOS
① output_flow = EOS                     正在送第 250 個 frame 給 VPU
② unlock(v4l2output)  ── 叫醒 ──→       process() 回傳 FLUSHING
                                        檢查 task 狀態
                                          在 ③ 之後 → PAUSED → 回傳 EOS ✓
                                          在 ③ 之前 → STARTED → 回傳 FLUSHING ✗
③ pause_task() → PAUSED
```

輸入 thread 用「task 狀態」判斷輸出 thread 是否已經結束，但 task 狀態要到 ③ 才更新，而叫醒輸入 thread 的 ② 在它前面。②、③ 之間正常只有幾 ms，系統忙的時候輸出 thread 可能在中間被排程換掉，輸入 thread 就會在這段空窗期做判斷，把 EOS 換成 FLUSHING。

### 4.2 為什麼原本這樣寫

FLUSHING 有兩種來源，這個判斷原本就是要分開它們：

1. 外部真的在 flush（seek、切換 state）：輸出 thread 還在跑，應該回傳 FLUSHING。
2. 輸出 thread 自己因 EOS 或錯誤結束：應該回傳真正的原因 `output_flow`。

追上游 git 歷史，2013 年的原始實作用一個 `processing` 旗標，而且在叫醒輸入 thread **之前**就清掉：

```c
  self->output_flow = ret;
  g_atomic_int_set (&self->processing, FALSE);
  gst_v4l2_object_unlock (self->v4l2output);
  gst_pad_pause_task (decoder->srcpad);
```

輸入 thread 檢查 `processing == FALSE`，所以沒有這個空窗。

2017 年的 commit `7b7a809818`（"v4l2dec: Fix race when going from PAUSED to READY"，bugzilla 778830）為了修另一個 state 切換的 race，拿掉 `processing` 旗標，改用 GstTask 狀態判斷（"to avoid duplicating the knowledge"）。改完之後，「標記已結束」從 ② 之前移到了 ② 之後，就留下了這段空窗。這是一個回歸。

### 4.3 為什麼需要「提早 EOS」加上負載才會發生

- 需要下游先回 EOS（`fakesink num-buffers`），輸出 thread 才會在輸入 thread 還在送 frame 時離開。影片自然播完的情況下，EOS 是從上游傳下來的，不會走到這條路，所以 D 階段完整解完 600 frames 都不會卡。
- 需要 ②、③ 之間被拉長，所以只有在 CPU 忙的時候才看得到。

這也解釋了為什麼一般長時間播放影片不容易重現：沒有用 `num-buffers` 讓 pipeline 提早結束，或是沒有加負載，都不會走到這條路。

### 4.4 為什麼不直接加 lock

把 ①②③ 和輸入端的檢查放在同一把 lock 裡也能解決，但最自然的選擇是 decoder 的 `STREAM_LOCK`，而：

- ③ 的 `gst_pad_pause_task` 會去拿 srcpad 的 `PAD_STREAM_LOCK`。
- flush 或切換 state 時，主 thread 會拿著 `STREAM_LOCK` 等輸出 thread 結束。

輸出 thread 如果在 ②③ 期間也要拿 `STREAM_LOCK`，就可能跟主 thread 互等。上游 issue #4057 就是 v4l2videodec 在這類鎖順序上造成的 deadlock。輸入 thread 真正需要的資訊是「輸出 thread 為什麼結束」，這個值在 ① 就寫進 `output_flow` 了，而且一定比 ② 早，所以改看 `output_flow` 就能修好，不需要新增 lock。

## 5. 解決方案

### 5.1 Patch

在 `handle_frame` 遇到 FLUSHING 時，除了原本的 task 狀態，也檢查 `output_flow` 是否已經是結束原因：

```diff
--- a/sys/v4l2/gstv4l2videodec.c
+++ b/sys/v4l2/gstv4l2videodec.c
@@ -1295,7 +1295,8 @@
 
     if (ret == GST_FLOW_FLUSHING) {
       if (gst_pad_get_task_state (GST_VIDEO_DECODER_SRC_PAD (self)) !=
-          GST_TASK_STARTED)
+          GST_TASK_STARTED || (self->output_flow != GST_FLOW_OK &&
+              self->output_flow != GST_FLOW_FLUSHING))
         ret = self->output_flow;
       goto drop;
     } else if (ret != GST_FLOW_OK) {
```

`output_flow` 的生命週期：

| 時機 | 值 |
|---|---|
| `start` / `stop` / `set_format` / `flush` | `GST_FLOW_OK` |
| `handle_frame` 啟動輸出 task 前 | `GST_FLOW_FLUSHING` |
| 輸出 loop 每輪開始 | `GST_FLOW_OK` |
| 輸出 loop 離開時 | 離開原因（EOS、NOT_NEGOTIATED、ERROR、FLUSHING） |

所以只有在輸出 loop 因 EOS 或錯誤離開後，`output_flow` 才會是 OK / FLUSHING 以外的值：

- 外部 flush：`output_flow` 是 OK 或 FLUSHING，新條件不成立，行為和原本一樣。
- 輸出 thread 因 EOS 或錯誤離開：不論 task 狀態是否已更新，都回傳真正原因。
- seek 之後 `flush` 會把 `output_flow` 重設成 OK，不影響之後的播放。
- 原本看 task 狀態的條件保留，2017 年修的 state 切換問題不受影響。

### 5.2 Yocto 整合

在 BSP layer 加一個 bbappend：

```text
recipes-multimedia/gstreamer/
├── gstreamer1.0-plugins-good_1.26.0.imx.bbappend
└── files/0001-v4l2videodec-return-the-processing-loop-flow-when-it.patch
```

```text
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " \
    file://0001-v4l2videodec-return-the-processing-loop-flow-when-it.patch \
"
```

注意：patch 的 hunk 要從乾淨的原始碼用 `diff -u` 重新產生，不要手動刪改 patch 內容。手動刪掉幾行會讓 hunk 上下文不對稱，`do_patch` 的 QA 會報 `Patch fuzz` 並中止。

### 5.3 驗證

把重編後的 `libgstvideo4linux2.so` 換到板子上，用和修改前完全相同的條件（stress-ng、250 frames、開 GST_DEBUG）跑 30 分鐘：

| | 修改前 | 修改後 |
|---|---|---|
| 解碼次數 | 314 | 690 |
| 卡住次數 | 4（約 1.3%） | 0 |
| 解碼失敗 | 0 | 0 |
| 平均時間 | 2.65 秒 | 2.46 秒 |
| 和 VPU 有關的 kernel 錯誤 | 無 | 無 |

修改後每次結束的 log 都是 `flow error eos`。如果 patch 無效、機率仍是 1.3%，連續 690 次都不卡的機率低於 0.02%。

## 6. 上游狀態

截至 2026-10 查詢的結果：

| 來源 | 狀態 |
|---|---|
| nxp-imx/gst-plugins-good `MM_04.10.02_2510_L6.12.49` | 未修，程式相同 |
| nxp-imx/gst-plugins-good `MM_04.11.01_2608_L6.18.37`（最新） | 未修，程式相同 |
| GStreamer main（gitlab.freedesktop.org） | 未修，程式相同 |
| 上游 issue / MR | 沒有找到相同的回報 |

相關但不同的上游紀錄：

| 編號 | 內容 | 差別 |
|---|---|---|
| issue #3522 | drain 與 source change 之間的 race | 不同路徑 |
| issue #4397 | 最後一個 buffer 出來後 poll 一直等 | 卡在 poll |
| issue #4057 | 切換 state 時 deadlock | 鎖順序問題 |
| issue #4983 | sdm845 影片結尾 qbuf 失敗 | driver 錯誤 |

## 7. 結論與建議

1. **「VPU 解碼逾時」不一定是 VPU 的問題。** 這次 VPU 硬體、wave6 driver 和 firmware 都正常，frame 早就解完了，問題在 GStreamer v4l2videodec 的結束流程。
2. **先量測再下結論。** 用 VPU 中斷數和 CPU 時間就能分辨「變慢」和「卡死」，再用 kernel stack 確認卡在 kernel 還是 userspace，可以很快把範圍縮小。
3. **GST_DEBUG 只保留出事的那次。** 每次都寫到 tmpfs，正常就刪掉，可以在不撐爆儲存空間的情況下抓到低機率的問題，同時保留一份正常的 log 作對照。
4. **影響範圍。** 只有下游先回 EOS 才會發生，例如 `num-buffers`、只取前幾個 frame 的測試或縮圖程式。一般播放器停止播放是把 pipeline 切到 NULL，不會走這條路。
5. **測試程式的暫時避開法。** BSP 還沒帶入 patch 之前，解碼測試可以拿掉 `num-buffers`，改成完整解完影片片段。
6. **上游。** 這是 2017 年 `7b7a809818` 造成的回歸，修法很小，適合送給 GStreamer 上游和 NXP。

## 附錄

### A. Probe 腳本

```bash
#!/usr/bin/env bash
out=$1 dur=$2
v=/path/to/bbb_480p60_h264_baseline_10s.mp4
mkdir -p "$out"
irq() { awk '/vpu_irq/{s=0; for(i=2;i<=7;i++) s+=$i; print s}' /proc/interrupts; }
end=$((SECONDS + dur)) n=0
while ((SECONDS < end)); do
	n=$((n+1)); t0=$(date +%s.%N); i0=$(irq)
	gst-launch-1.0 -q filesrc location=$v ! qtdemux ! h264parse ! v4l2h264dec ! \
		fakesink sync=false num-buffers=250 >/dev/null 2>&1 &
	pid=$!; slow=0
	while kill -0 $pid 2>/dev/null; do
		sleep 1
		el=$(awk -v a=$t0 -v b=$(date +%s.%N) 'BEGIN{printf "%d", b-a}')
		if ((el >= 60 && slow == 0)); then
			slow=1; s="$out/slow_$n"; mkdir -p "$s"
			for k in 0 1 2 3 4 5; do
				echo "$(date +%T) irq=$(irq) cpu=$(awk '{print $14+$15}' /proc/$pid/stat)" >>"$s/irq_trace"
				sleep 10
			done
			for t in /proc/$pid/task/*; do
				echo "== ${t##*/} $(cat $t/comm) $(cat $t/wchan)"; cat $t/stack
			done >"$s/stacks" 2>&1
			gdb -p $pid -batch -ex "thread apply all bt" >"$s/gdb_bt" 2>&1
			dmesg | tail -50 >"$s/dmesg"
		fi
		((el >= 300)) && { kill -9 $pid; break; }
	done
	wait $pid; rc=$?
	dt=$(awk -v a=$t0 -v b=$(date +%s.%N) 'BEGIN{printf "%.2f", b-a}')
	echo "$n $dt $rc $(( $(irq) - i0 ))" >>"$out/decodes.txt"
done
```

背景負載：

```bash
stress-ng --cpu 6 --vm 2 --vm-bytes 2G --io 2 --timeout 1830s &
```

### B. 常用指令

```bash
grep vpu_irq /proc/interrupts
cat /proc/<pid>/task/*/wchan
cat /proc/<pid>/task/*/stack
gdb -p <pid> -batch -ex "thread apply all bt"
grep -n -E "we are EOS|Leaving output thread|flow error|Changing task" gst_debug.log
```

### C. 參考資料

- nxp-imx/gst-plugins-good：`sys/v4l2/gstv4l2videodec.c`
- GStreamer commit `7b7a809818`：v4l2dec: Fix race when going from PAUSED to READY（bugzilla 778830）
- GStreamer commit `61183670c0`：v4l2videodec: Implement v4l2videodec（原始的 `processing` 旗標實作）
