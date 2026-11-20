# i.MX95 Android 長影片循環播放 media.extractor 崩潰分析（NXP mp4 parser 無號數減法觸發 UBSan）

## 1. 問題概述

一片 i.MX95 SMARC 自製板移植到 Android 16 後，跑 12 小時燒機測試（StressTest app）時，VIDEO 測項失敗 7 次。每次都是影片播完、從頭重播的瞬間，畫面跳出「Can't play this video」，之後就不再播放。

已知的現象：

- 第一次播放完全正常，影像走 VPU 硬體解碼，畫面和聲音都沒問題。
- 每次失敗都發生在 `onCompletion` 之後約 1 秒，也就是重播的時候。
- 失敗當下 `media.extractor` 程序 abort，留下 tombstone，`Abort message: 'ubsan: sub-overflow ...'`。
- 同一個測試影片，在另一片 i.MX8MP 板（同為 Android 16）上也能重現。
- 那片 i.MX8MP 板先前跑過 12 小時同樣的測項卻是 PASS，後來確認當時用的是短影片。

一開始懷疑是系統負載（同時跑 CPU / 記憶體 / USB 等測項）造成。實際上和負載無關：**NXP 的 mp4 parser 在「往回」載入 sample size 表時，用無號數減法算起點，結果繞回（wrap），而這個 library 開了整數溢位的 sanitizer，一繞回就 abort**。只有音訊（或影像）sample 數超過 18000 的檔案才會走到這段程式碼，所以只有長影片會出事。

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55），另以 i.MX8MP 板交叉驗證 |
| BSP | NXP android-16.0.0_2.0.0 |
| Parser | `vendor/nxp-opensource/imx-mp4-parser`（MM_04.11.00_2605，執行時印出 `MPEG4PARSER_07.00.07`），編成 `lib_mp4_parser_arm11_elinux.3.0.so` |
| Extractor | `libimxextractor.so`（`imx_android_mm/extractor`），由 `vendor.media.fsl_codec.flag` 的 bit 0 控制是否啟用 |
| 解碼 | Codec2 `c2.imx.avc.decoder`（VPU）、`c2.android.aac.decoder` |
| 測試 app | StressTest 2.0.12，VIDEO 測項用 `VideoView` 播放，`onCompletion` 時呼叫 `start()` 從頭重播 |
| 測試影片 | `bbb_full.ffmpeg.720x480.mp4.libx264_500kbps_25fps.libfaac_stereo_128kbps_44100Hz.mp4`，596 秒；影像 14315 個 sample，音訊（AAC，44.1 kHz）25686 個 sample |

`vendor.media.fsl_codec.flag` 各 bit 的意義（板子的 `init.rc` 預設設成 7）：

| bit | 讀取的程式 | 作用 |
|---|---|---|
| 0 | `ImxInspector.cpp` | 1：由 NXP extractor（`libimxextractor`）解析檔案；0：交給 AOSP extractor |
| 1 | `ImxC2Store.cpp` | 1：NXP 影像 codec（VPU）優先 |
| 2 | `ImxC2Store.cpp` | 1：NXP 音訊 codec 優先 |

## 3. 除錯過程

### 3.1 崩潰現場

tombstone 的 backtrace：

```text
Abort message: 'ubsan: sub-overflow ...'
  #03 lib_mp4_parser_arm11_elinux.3.0.so (load_new_entry_u32)
  #04 lib_mp4_parser_arm11_elinux.3.0.so (getSampleSizeAndOffset)
  #09 libimxextractor.so (ImxExtractor::GetNextSample)
```

logcat 的順序：

```text
VideoTestFragment: onCompletion, restart
ImxExtractor: HandleSeekOperation BEGIN index=0,target=0,flag=2
F DEBUG   : Abort message: 'ubsan: sub-overflow by 0x0000006eb829286c'
MediaPlayerService: media.extractor died. Sending death notification.
VideoView: Error: 100,1
VideoView: Error: 1,-32   （之後反覆出現）
```

`media.extractor` 死掉後，播放器拿到 `MEDIA_ERROR_SERVER_DIED`，無法自行恢復。app 在 `onError` 裡記一次失敗、再呼叫 `start()`，但播放器已經失效，只會疊出更多「Can't play this video」對話框。

### 3.2 排除系統負載

把其他測項全部停掉，只留 VIDEO，在閒置狀態下播同一個影片：第一次重播一樣 crash。負載不是條件。

### 3.3 找出觸發條件：sample 數門檻

把原始影片裁成不同長度、分離影像和聲音，各播到重播：

| 檔案 | 影像 sample | 音訊 sample | 重播結果 |
|---|---|---|---|
| 原始檔（596 s） | 14315 | 25686 | crash |
| 只有影像（596 s） | 14315 | — | 正常 |
| 只有聲音（596 s） | — | 25686 | crash |
| 裁短，音訊 17959 frame | — | 17959 | 正常 |
| 裁短，音訊 18002 frame | — | 18002 | crash |

門檻剛好落在 18000 個 sample。AAC 每個 frame 1024 個取樣點，換算成時間：

| 音訊取樣率 | 18000 frame 的長度 |
|---|---|
| 44.1 kHz | 約 6 分 58 秒 |
| 48 kHz | 約 6 分 24 秒 |

### 3.4 為什麼另一片板子「之前測過沒問題」

另一片 i.MX8MP 板用同一個 parser，拿同一個長影片測也是重播就 crash（StressTest 顯示 Failed 6）。它先前 12 小時 PASS 的那次，事後確認用的是短影片，沒有超過門檻。

測試時還發現一個會掩蓋問題的情況：把 `audioserver` 停掉，讓音訊輸出開不起來，同一個長影片就能正常重播。原因是 NuPlayer 開不了 audio sink（`openAudioSink` 回傳 -19）時會把音訊 decoder 關掉，之後不再讀音訊軌，也就不會去載入音訊的 sample size 表。所以**沒接音訊輸出、或音訊路徑有問題的板子，可能跑長影片也不會 crash**，比對測試結果時要注意這點。

## 4. Root Cause 分析

### 4.1 背景與術語

| 術語 | 白話 |
|---|---|
| stsz | mp4 的 Sample Size box，記錄每個 sample（一個影像 frame 或一個 AAC frame）的大小，一個 sample 一筆 |
| 分段載入 | parser 不一次把整張 stsz 表讀進記憶體，最多只放 `STSZ_TAB_MAX_SIZE` 筆，需要時再換一段 |
| overlap | 換段時保留前一段的一部分，避免在段落邊界來回切換時反覆讀檔 |
| UBSan | Undefined Behavior Sanitizer，編譯器在算式加上檢查，發現溢位等狀況時直接 abort |

`SampleSizeAtom.c` 的設定：

```c
#define STSZ_TAB_MAX_SIZE 18000
#define STSZ_TAB_OVERLAP_SIZE (STSZ_TAB_MAX_SIZE / 10)   /* 1800 */
```

一條軌的 sample 數 ≤ 18000 時，整張表一次載入，`load_new_entry_u32()` 一開頭就 return，不會進到後面的計算。這說明了為什麼只有影像（14315 個 sample）的檔案沒事。

### 4.2 根因

`MP4TableLoad.c` 的 `load_new_entry_u32()`，往回載入（目標 sample 在目前這段之前）的分支：

```c
    } else /* backward */
    {
        new_end_entry_idx = target_entry_idx + tab_ovlp_size + 1;
        if (new_end_entry_idx > nb_total_entries)
            new_end_entry_idx = nb_total_entries;

        new_start_entry_idx = new_end_entry_idx - tab_size;      /* u32 減法 */
        if (0 > (s32)new_start_entry_idx) /* check left boundary */
        {
            new_start_entry_idx = 0;
            ...
        }
    }
```

所有變數都是 `u32`。程式的寫法是**故意讓減法繞回**，再轉成 `s32` 判斷是不是負數。在一般編譯下這樣可以運作，但 imx-parser 的共用設定開了無號數溢位檢查：

```text
vendor/nxp-opensource/imx-parser/Android.bp
cc_defaults {
    name: "imx_parser_defaults",
    sanitize: {
        misc_undefined: [
            "signed-integer-overflow",
            "unsigned-integer-overflow",
            "bounds",
        ],
    },
}
```

`imx-mp4-parser/src/mp4_parser/Android.bp` 的 `lib_mp4_parser_arm11_elinux.3.0` 使用 `imx_parser_defaults`，所以這個 library 的 u32 減法一旦繞回就會 abort。

用測試影片的音訊軌代入：

```text
播到結尾時，記憶體內是最後一段：old_start_entry_idx ≈ 25686 - 18000 = 7686
重播 → seek 到 0 → target_entry_idx = 0 < old_start  → 走 backward

new_end_entry_idx   = 0 + 1800 + 1 = 1801
new_start_entry_idx = 1801 - 18000            → u32 繞回 → UBSan abort
```

事實整理：

1. 只要某一條軌的 sample 數超過 18000，而且需要往回載入、目標 index 小於 16199（18000 − 1800 − 1），就一定會繞回。重播（seek 到 0）一定符合這個條件。
2. 同一個檔案裡有好幾處相同寫法（`load_next_tab_section_u32`、`load_new_entry`、`load_new_entry_u32`、`load_new_entry_u64`、`load_new_entry_dwords`，共 9 處 `x = a - b; if (0 > (s32)x) x = 0;`）。
3. 從程式推論，以下情況也會觸發，但這次沒有實測：往回 seek 到前 16199 個 sample 以內；只有影像但 frame 數超過 18000 的影片（25 fps 約 12 分鐘、30 fps 約 10 分鐘）。

### 4.3 為什麼 NXP 自己沒抓到

- sanitizer 從 Android 16 imx-parser 第一版（2026-03）就開著，當時是 diag 模式。
- MA-24637（2026-04，`imx-parser` 32967a0、`imx-mp4-parser` feb234f）修了一批 UBSan 回報的問題，同時把 diag 模式拿掉，改成直接 abort。mp4 parser 這次只改了 `MP4Atoms.c`、`MP4Parser.c`，`MP4TableLoad.c` 沒有動。
- 要觸發，必須「某條軌超過 18000 個 sample」加上「往回載入」。短的測試片、只往前播一次的測試，都不會走到。

## 5. 解決方案

### 5.1 目前採用：改用 AOSP extractor（flag 7 → 6）

板子的 `init.rc`：

```diff
-    setprop vendor.media.fsl_codec.flag 7
+    setprop vendor.media.fsl_codec.flag 6
```

把 bit 0 清掉，檔案改由 AOSP 的 extractor（`MPEG4Extractor` 等）解析。bit 1、bit 2 保留，解碼仍然走 NXP 的 Codec2 component，影像仍是 VPU 硬體解碼。

代價是 NXP parser 才支援的容器（AVI、FLV、RMVB、ASF/WMV、APE 等）不能再播；mp4、mkv、webm、ts、mp3 由 AOSP extractor 處理，不受影響。

### 5.2 根本修正（評估中）：修 parser 的減法

把 9 處「先減、再轉 s32 判斷」改成「先比較、再相減」，邏輯結果和原本一樣，只是不再溢位：

```c
/* 原本 */
new_start_entry_idx = new_end_entry_idx - tab_size;
if (0 > (s32)new_start_entry_idx)
    new_start_entry_idx = 0;

/* 改成 */
new_start_entry_idx = (new_end_entry_idx > tab_size) ?
                      new_end_entry_idx - tab_size : 0;
```

這樣可以保留 NXP parser 支援的所有格式，所有板子一次修好。但 `imx-mp4-parser` 目前直接 sync NXP 的 GitHub，要改就得自行 fork、修改 manifest，之後 NXP 升版時還要 rebase；或者回報 NXP，等上游修正。

### 5.3 考慮過但沒採用的做法

| 做法 | 問題 |
|---|---|
| 拿掉 `imx_parser_defaults` 的 `unsigned-integer-overflow` | 等於回到沒有檢查的狀態，會連其他 parser 的檢查一起關掉，蓋掉其他真正的溢位 |
| 只改 StressTest，重播前重新 `setVideoPath()` | 只繞過測試 app，一般播放器往回 seek 一樣會 crash |
| 換短的測試影片 | 測不到問題，產品上的長影片照樣會 crash |

### 5.4 驗證結果

| 項目 | 方法 | 結果 |
|---|---|---|
| flag 6，i.MX95 | 同一個 596 秒影片，連續重播 3 次 | 不再 crash；`logcat` 沒有 `ImxExtractor`，影像由 `c2.imx.avc.decoder` 解碼，`vpu` 中斷數持續增加 |
| flag 6，i.MX8MP | 同一個影片重播 | 06:02:15 重播，沒有 crash、沒有新的 tombstone，`hantrodec` 中斷數持續增加 |
| flag 7，i.MX8MP，停掉 `audioserver` | 同一個影片重播 | 不會 crash（音訊軌沒有被讀取，見 §3.4） |
| 門檻 | 裁剪後的音訊長度 | 17959 frame 正常、18002 frame crash |

## 6. 上游狀態

- 在本 BSP 版本（MM_04.11.00_2605）中，`MP4TableLoad.c` 的 9 處寫法都還在，NXP 尚未修正。
- MA-24637 處理過 mp4 parser 的 UBSan 問題，但沒有涵蓋 `MP4TableLoad.c`。
- 目前兩片板子都先改用 flag 6。parser 的根本修正方式（§5.2）已整理給 reviewer，再決定要自行 fork，還是回報 NXP。

## 7. 結論與建議

1. **「故意繞回再判斷正負」的寫法，和 UBSan 不相容。** 對無號數做減法前要先比大小。開啟 `unsigned-integer-overflow` 之前，要先把這類寫法掃過一遍。
2. **sanitizer 會把潛藏的邏輯問題變成直接 crash。** 一般 build 下會正確處理的程式，開了 sanitizer 就會 abort。媒體 library 這類會接收各種外部檔案的元件，要用涵蓋邊界條件的檔案測過再出貨。
3. **burn-in 測試的影片要夠長。** 至少要讓每一條軌都超過 parser 的分段門檻（這裡是 18000 個 sample，44.1 kHz 的 AAC 約 7 分鐘），而且要包含重播和往回 seek。
4. **比對兩片板子的測試結果時，先確認條件一樣。** 這次「另一片板子沒問題」的原因是測試影片不同。另外，沒有音訊輸出也會讓問題消失，同樣是會造成誤判的條件。
5. **extractor 和 decoder 是分開的兩層。** 改用 AOSP extractor 不影響硬體解碼，可以當作 parser 問題的低風險暫時解法。

## 附錄

### A. 常用指令

```bash
getprop vendor.media.fsl_codec.flag
setprop vendor.media.fsl_codec.flag 6; killall media.extractor   # 不重開機切換（重開後回到 init.rc 的值）

dumpsys media.extractor | grep -i "IMX Extractor"
logcat -d | grep -iE "ImxExtractor|MPEG4Extractor|CCodec.*allocate"
logcat -d | grep -E "onCompletion|ubsan|media.extractor died|VideoView.*Error"
ls -l /data/tombstones
grep -iE "vpu|hantro" /proc/interrupts

ffprobe -v error -select_streams a:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 in.mp4
```

### B. 參考資料

- `vendor/nxp-opensource/imx-mp4-parser/src/mp4_parser/lib/src/MP4TableLoad.c`（`load_new_entry_u32`、`load_next_tab_section_u32`、`load_new_entry`、`load_new_entry_u64`、`load_new_entry_dwords`）
- `vendor/nxp-opensource/imx-mp4-parser/src/mp4_parser/lib/src/SampleSizeAtom.c`（`STSZ_TAB_MAX_SIZE`、`STSZ_TAB_OVERLAP_SIZE`）
- `vendor/nxp-opensource/imx-parser/Android.bp`（`imx_parser_defaults` 的 `sanitize`）
- `vendor/nxp-opensource/imx_android_mm/extractor/ImxInspector.cpp`、`codec2/store/ImxC2Store.cpp`（`vendor.media.fsl_codec.flag`）
- imx-parser commit `32967a0`、imx-mp4-parser commit `feb234f`：MA-24637 Fix issues reported by UndefinedBehaviorSanitizer
