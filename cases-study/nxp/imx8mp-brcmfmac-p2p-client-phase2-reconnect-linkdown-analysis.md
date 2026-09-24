# i.MX8MP brcmfmac P2P client phase-2 重連失敗分析（WSC 假 link-down）

> 本案為 [`imx8mp-brcmfmac-p2p-wifi-direct-bringup-analysis.md`](imx8mp-brcmfmac-p2p-wifi-direct-bringup-analysis.md)
> 的後續。該篇的 8 個 patch 讓 P2P 雙向「一次成功」通過驗收；本案處理的是其後浮現、
> **只發生在板子當 client** 的殘留間歇性失敗。

## 1. 問題概述

WiFi Direct bringup 完成後，板子當 P2P **client** 連手機時，仍有一個**間歇性**卡點：
WSC（phase 1）已經成功、拿到 WPA2 憑證，但在 **phase 2（用該憑證做 WPA2 重連）** 卡住，
最後 group formation 失敗。板子當 GO 的方向一直正常。

典型失敗症狀（板子當 client、手機當 GO、5GHz）：

```text
P2P-GO-NEG-SUCCESS role=client freq=5180
Trying to associate with <go-mac>
WPS-SUCCESS                               ← phase 1（WSC）成功
CTRL-EVENT-DISCONNECTED reason=3          ← GO deauth，正常的 phase1→2 過渡
Trying to associate with <go-mac>         ← phase 2（WPA2 重連）
CTRL-EVENT-ASSOC-REJECT status_code=16    ← 重連被判失敗
...
brcmfmac: brcmf_run_escan: error (-52)    ← 之後 client-bsscfg 重掃 -52 迴圈
P2P-GROUP-FORMATION-FAILURE
P2P-GROUP-REMOVED ... reason=FORMATION_FAILED
```

盤點：

- **phase 1（WSC PBC）永遠成功**，卡點固定在 phase 2 的 WPA2 重連。
- wpa 端看到的是 `ASSOC-REJECT status_code=16`，接著 driver 端 `brcmf_run_escan: error(-52)` 反覆出現。
- **時好時壞**：bringup 當天（2026-08-26）能通、後來（2026-09-14 起）在乾淨的 OTA release（R82）上
  變成穩定重現的硬失敗 —— 這個「從偶爾到必現」的轉變是解題的重要線索。
- 一般 `wlan0` STA 掃描完全正常，只有 **P2P client bsscfg 的重掃**吃 -52 —— 說明 escan 沒有全域壞掉。

## 2. 系統環境

| 項目 | 版本 / 型號 |
|---|---|
| 平台 | i.MX8MP（客製載板，Android BSP） |
| BSP | NXP **android-16.0.0_2.0.0**，linux-imx **6.18**（R82 = `6.18.32-gef14aa5c`） |
| WiFi/BT 模組 | AMPAK AP6275（**BCM43752**，SDIO） |
| WiFi 韌體 | v18.35.387.23.108（2022） |
| Driver | mainline **brcmfmac**（module，`=m` / `vendor_dlkm`） |
| P2P 框架 | Android `wifip2p` / wpa_supplicant |
| 測試對端 | Android 手機（當 P2P GO） |
| 基準 | 已含 bringup 的 8 個 P2P commit（本案為其疊加的第 9 個修正） |

P2P client 入群分兩段：**(1) EAP-WSC（PBC）** 換 WPA2 憑證；**(2)** 用該憑證做 **WPA2-PSK 4-way**。
兩段之間 GO 會 deauth 一次，wpa 用新憑證重新關聯（即 phase 2）。本案的卡點就在這個「重連」的交界。

## 3. 除錯過程

### 3.1 先確認是真 bug、非污染

bringup 期間大量 live-insmod 測試可能留下髒狀態，因此先取一個**乾淨基準**：把板子 OTA 到 R82
（已 commit 的 8-patch P2P、全新 release、無 live-insmod 污染），重測 client。結果**照樣失敗、可穩定重現**
→ 排除「測試污染」，確認是真 bug。GO 方向在 R82 同樣正常。

### 3.2 診斷基礎建設（帶標籤 print + 免重燒的 live-insmod）

在 `cfg80211.c` / `fweh.c` 的關鍵路徑加 `P2P2DBG` 的 `pr_info`，只印 P2P client 相關事件：

- `brcmf_notify_connect_status` 進入時：`event / status / flags / sme_state /
  is_linkup / is_linkdown / is_nonetwork`
- `brcmf_cfg80211_connect` 進入時：`iftype / ssid_len / chan / key_mgmt`
- `brcmf_run_escan` 失敗時：`err / ifidx / bsscfgidx / iftype`
- fweh IF_DEL：`ifidx / bsscfgidx / role / armed`

因為板子 verity-enforcing、bootloader locked（`vendor_dlkm` 不能換），但 `MODULE_SIG_FORCE` 未設，
採 **live-insmod** 疊代（免重燒）：

- `cook kernel` 出 `brcmfmac.ko`，**binary-patch modinfo vermagic** 去掉未提交造成的 `-dirty`
  後綴（同長度 null 補齊），才能 insmod 到 `6.18.32-gef14aa5c` 的板子。
- 換模組順序（重要）：`setenforce 0` → **先 unbind SDIO 裝置** →
  `rmmod brcmfmac-{bca,cyw,wcc}` + `rmmod brcmfmac` → `insmod` 新 `.ko` → 重載 3 個 fwvid 子模組 →
  **再 bind SDIO**。若在 SDIO 裝置還綁著時 insmod，probe 會在 fwvid handler 註冊前跑 → `brcmf_attach failed`、
  無 `wlan0`，之後 bind 也救不回、只能重開機。

### 3.3 精準 trace：卡點在一個「假的 link-down」

在 phase 2 重連的當下，`P2P2DBG` 印出決定性的一段：

```text
connect ENTER iftype=8 key_mgmt=1                 ← phase-2（WPA2）連線進入
connstat event=16 status=0 flags=0x0 sme=0x3 up=0 down=1   ← 一個「link-down」
connstat event=16 status=0 flags=0x1 sme=0x3 up=0 down=0
connstat event=0  status=0 flags=0x0 sme=0x3 up=1 down=0   ← 真正的 link-up
```

關鍵在第二行：phase 2 一開始，韌體先丟出一個 **`E_LINK`（event=16）、flags=0x0（沒有 link-up 位）**
的事件，`brcmf_is_linkdown()` 判為 **link-down（down=1）**，而此時 `sme_state=0x3`
（`CONNECTING` 已設、`CONNECTED` 未設 —— 還在連線中）。**緊接著**才來真正的 link-up（第 3、4 行）。

也就是說：這個 link-down 是**短暫、虛假的**，它出現在真正 link-up 的**前一瞬間**。

### 3.4 driver 把假 link-down 當成連線失敗

`brcmf_notify_connect_status()` 的 linkdown 分支不分青紅皂白：只要 `is_linkdown`，
就往 `brcmf_bss_connect_done(cfg, ndev, e, false)` 走（回報連線失敗）。於是流程變成：

```text
phase-2 假 link-down → brcmf_bss_connect_done(false)
   → cfg80211 回報連線失敗 → wpa 收到 ASSOC-REJECT status_code=16
   → wpa 觸發「復原重掃」→ 對 P2P client bsscfg 發 escan
   → 此韌體對 client bsscfg 的這次重掃回 -52 → 迴圈 → FORMATION_FAILED
```

**這解釋了「為何時好時壞」**：phase 2 本來就是「吃一次 reject、再 retry」的節奏；早期能通，
是因為那次 retry 依賴的復原重掃剛好成功。後來（RF/5GHz 壅塞、對端 GO 行為差異等）
**復原重掃每次都 -52**，retry 再也起不來 → 從偶爾失敗變成必現。

## 4. Root Cause 分析

**`escan -52` 是下游症狀，不是根因。** 真正的根因是：**phase-2 重連時韌體發的短暫假 `E_LINK`-down，
被 driver 當成連線失敗上報**，中止了本來會成功的重連。

- 這顆 BCM43752 韌體在 P2P client 的 WSC phase-1 → phase-2 交界，會發一個 flags=0x0 的
  bare `E_LINK` 事件（`brcmf_is_linkdown` 判為 down），**緊跟著才是真正的 link-up**。
- 通用 STA 路徑沒有「還在 CONNECTING 時可能先來一個假 down、之後才 up」的概念，
  於是把這個 down 當作連線失敗（`brcmf_bss_connect_done(false)`）。
- 一旦回報失敗，wpa 走復原重掃 → 撞上「P2P client bsscfg 重掃 → 韌體 -52」這個已知 quirk → 死迴圈。

換句話說：只要**不要**把這個 transient down 當失敗，後面真正的 link-up 就會把 phase 2 正常收尾，
**根本不需要那次會 -52 的復原重掃**。這與 bringup 篇 §5 的思路一致 —— 對齊「韌體真正的狀態」，
不要對 P2P bsscfg 做通用 STA 路徑會做、但這顆韌體受不了的事。

> 與 bringup #7（`keep a P2P client interface across a spurious IF_DEL`）的關係：兩者都是
> phase-2 交界的**同一類韌體行為**（虛假事件）的不同面向 —— #7 擋的是虛假的 **IF_DEL**（會拆介面），
> 本案擋的是虛假的 **E_LINK-down**（會被當連線失敗）。兩者互補、都需保留。

## 5. 解決方案

### 5.1 修正 patch（單檔 8 行）

在 `brcmf_notify_connect_status()` 的 linkdown 分支最前面加一個守衛：**當對象是 P2P client、
事件是 `E_LINK`、且仍在「CONNECTING 但尚未 CONNECTED」的狀態時，忽略這個 link-down、直接 return**，
不要走 `brcmf_bss_connect_done(false)`。後面隨即到來的真正 link-up 會把連線完成。

```c
} else if (brcmf_is_linkdown(ifp->vif, e)) {
	brcmf_dbg(CONN, "Linkdown\n");
	/* this fw emits a transient E_LINK-down on the P2P-client WSC
	 * phase-2 reconnect; ignore it while connecting (not a failure).
	 */
	if (ifp->vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT &&
	    e->event_code == BRCMF_E_LINK &&
	    test_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state) &&
	    !test_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state))
		return err;
	...
```

要點：

- **只在 P2P client 生效**（`iftype == NL80211_IFTYPE_P2P_CLIENT`）；板子當 GO 時 iftype 為
  `P2P_GO`，這段永遠不執行 → **GO 方向零影響**。
- 只忽略「CONNECTING 且尚未 CONNECTED」期間的 `E_LINK`-down；已連上後真正的斷線
  （CONNECTED 已設）照常處理，不會漏掉真的 disconnect。
- 診斷用的 `P2P2DBG` print 全數移除，最終只留這段忽略邏輯 + 兩行說明註解。

### 5.2 驗證結果（adb 實測，2026-09-16，R82 / `6.18.32-gef14aa5c`）

**client 方向（`cmd wifip2p connect <mac> -i 0`，連兩次）：**

```text
connstat event=16 flags=0x0 down=1        ← 假 link-down（fix 觸發、忽略）
connstat event=0  up=1                     ← 真正 link-up → phase-2 繼續
WPS-SUCCESS → WPA: Key negotiation completed [PTK=CCMP GTK=CCMP]
P2P-GROUP-STARTED p2p-wlan0-N client freq=5180 / 5745
DHCP → 192.168.49.94（第二次 .226）
ping 192.168.49.1（GO）→ 0% loss ✓    ← 全程無 escan -52 迴圈
```

- **兩次皆成功。** 且觀察到假 link-down 是**間歇性**的：第 1 次有（down=1、fix 觸發）、
  第 2 次沒有（down=0、走乾淨路徑）—— fix 擋掉壞 case、不動好 case。
- **GO 方向不受影響**：另建 autonomous GO、手機加入、DHCP `192.168.49.66`、ping 0%，
  整段 `P2P2DBG` 零觸發（印證 fix 只作用在 P2P_CLIENT）。
- 附註：連線後偶見 `brcmf_cfg80211_get_channel: chanspec failed(-52)` 是**另一個良性的 -52**
  （查 channel 用），與本案的 escan 失敗迴圈無關，連線已穩、ping 通。

### 5.3 狀態

- 修正為**單一 commit**、疊加在 bringup 的 8-commit P2P 系列之上：
  `wifi: brcmfmac: ignore transient link-down on P2P-client phase-2 reconnect`。
- 標題沿用該系列的 `wifi: brcmfmac:` 慣例，帶說明性註解、可獨立 review，
  隨同系列一併投稿 linux-wireless。
