# i.MX8MP brcmfmac WiFi Direct（P2P）雙向打通分析（BCM43752 SDIO）

## 1. 問題概述

i.MX8MP 平台從 vendor bcmdhd driver 遷移到 mainline brcmfmac 後，一般 STA 連線正常，
但 **WiFi Direct（P2P）完全不通**。產品驗收要求 P2P **雙向**（板子當 client 連手機、板子當 GO 被手機連）都要成立，一定得打通。

典型失敗症狀（板子當 P2P client 連手機）：

```
P2P-GO-NEG-SUCCESS role=client freq=5745 ...
P2P-GROUP-FORMATION-FAILURE
P2P-GROUP-REMOVED wlan0 client reason=FORMATION_FAILED
```

以及韌體端隨機崩潰（serial console）：

```
brcmfmac: brcmf_fil_cmd_data: WLC_SET_PROMISC (10) ... failed
brcmfmac: firmware ... trap / no response
```

主要症狀盤點：

- P2P discovery（找到 peer）大致可用，但一旦進入 **group formation 就失敗**
- 板子當 client 時，WSC（WPS）階段拿不到憑證，或 4-way 前韌體崩潰
- 韌體對某些 iovar/命令在 **P2P bsscfg「還沒 up」的狀態下** 送出即 trap
- 同一片板子、同一顆韌體，跑**舊 BSP（bcmdhd driver）P2P 雙向完全正常** —— 這是關鍵對照組
- 症狀多變、時好時壞，一度看似「時序 / RF」問題，實際多為 driver 對韌體下錯命令

## 2. 系統環境

| 項目 | 版本 / 型號 |
|---|---|
| 平台 | i.MX8MP（客製載板，Android BSP） |
| BSP（本案） | NXP **android-16.0.0_2.0.0**，linux-imx 6.12，**brcmfmac**（mainline，module） |
| 對照 BSP（正常） | Android 15，同板同韌體，**bcmdhd**（vendor fullmac，P2P 正常） |
| WiFi/BT 模組 | AMPAK AP6275（**BCM43752**，SDIO） |
| WiFi 韌體 | **v18.35.387.23.108（2022）**，兩版 BSP 共用同一顆 |
| P2P 框架 | Android `wifip2p` / wpa_supplicant（overlay conf） |
| 測試對端 | Android 手機（P2P Group Owner / Client 皆測） |

brcmfmac 是 **SDIO fullmac 但走 cfg80211 SME** 的 driver：主要連線邏輯（scan / connect /
4-way handshake 觸發）在 host 的 cfg80211 + wpa_supplicant，韌體負責 MAC/PHY。bcmdhd 則是把更多
邏輯放韌體。兩者對「同一顆韌體」下命令的時機與集合不同 —— 這是本案所有 bug 的根源。

## 3. 除錯過程

### 3.1 診斷基礎建設

先在 driver 關鍵路徑加上帶標籤的 `pr_info`（`P2PDBG` / `FWCMD` / `IFEVENT`），
從 serial console（`/dev/ttyUSB0` 115200，`echo 8 4 1 7 > /proc/sys/kernel/printk`）觀察：

- 韌體命令流（哪一個 iovar/ioctl 送出後韌體 trap）
- fweh IF event（IF_ADD / IF_DEL、role、NOIF flag）
- P2P bsscfg 狀態轉換（create / up / down）

host 端則用 `logcat` 抓 wpa_supplicant 的 control event（`P2P-*`、`WPS-*`、`WPA:`、`CTRL-EVENT-*`），
測試以 `su 0 cmd wifip2p`（init / start-peer-discovery / list-peers / connect / remove-group）腳本化，
連線成功後以 `ping 192.168.49.x`（P2P 子網）作為最終判準 —— **只有 ping 通才算 PASS**。

### 3.2 關鍵對照組：a15（bcmdhd）blueprint

因為是「同板同韌體、換 driver」，最高價值的資訊是 **bcmdhd 對這顆韌體到底下了哪些命令、順序為何**。
燒回 Android 15，抓 bcmdhd 的完整 P2P client 兩階段流程（WSC → WPA2）：

```
（a15 fresh trace，client formation 全程約 1.25s，一次成功）
wlan: wl_cfgp2p_set_firm_p2p → apsta 已在 init 設好，未再 WLC_DOWN
wlan: GC bsscfg：roam_off + buf_key_b4_m4 + wsec_info(BSS_ALGOS)
wlan: 不對 GC bsscfg 送 WLC_SET_PROMISC / mcast / ARP-ND
wlan: link-down 不送 WLC_DISASSOC（interface 直接 p2p_ifdel）
```

這份 blueprint 成為後續每一個修正的正解依據 —— **「brcmfmac 該怎麼下命令」= 「bcmdhd 對同顆韌體怎麼下」**。

### 3.3 排除法與單點修正

| 觀察到的 fw trap / 失敗 | 定位 | 對應修正 |
|---|---|---|
| P2P device 介面建不出來（韌體不發 IF_ADD event） | fweh `is_p2pdev` 判斷 + create 逾時直接 fail | 加 `p2p_dev` iovar 輪詢 fallback，手動 `brcmf_add_if` |
| P2P 設定路徑對 active 韌體送 `WLC_DOWN` → trap | `brcmf_p2p_set_firmware` 無條件 down/up | init 先設 `apsta=1`，之後判斷已設就跳過 down/up |
| 新建、未 up 的 **GO** bsscfg 送 `roam_off`/`SCB_TIMEOUT` → trap | `brcmf_p2p_add_vif` 對所有 iftype 都送 | GO 不送（roaming 是 STA 概念）；client 才送、並補 `buf_key_b4_m4` |
| P2P client bsscfg 送 `WLC_SET_PROMISC(cmd=10)` / mcast / ARP-ND → trap | `_brcmf_set_multicast_list` / `configure_arp_nd_offload` 對 P2P 也跑 | 對未 up 的 P2P bsscfg 一律延後/略過 data-plane iovar |
| link-down / disconnect 對 P2P client 送 `WLC_DISASSOC` → trap | `brcmf_link_down` / `.disconnect` | P2P client 跳過 DISASSOC（介面靠 p2p_ifdel 拆） |
| WSC 階段 GO deauth 後韌體發**假的** IF_DEL → 介面被拆、WPA2 重連斷 | fweh 預設 unarmed IF_DEL 就 remove | P2P client 的 unarmed IF_DEL 視為 spurious，保留介面 |
| EAPOL（WPS/4-way）在 netdev 還沒 `IFF_UP` 時被丟棄 → WSC 卡住 | `brcmf_netif_rx` 未 up 直接 free skb | EAPOL（`ETH_P_PAE`）永遠往上送 |
| P2P scan 用舊 escan 格式，部分情境掃不到 GO | `brcmf_p2p_run_escan` | 韌體支援時改用 escan V2 |

### 3.4 最大的坑：自己製造的複雜度（self-inflicted tangle）

在還沒抓到 a15 blueprint、也還沒定位「韌體對未 up bsscfg 會 trap」之前，為了硬讓 client 連上，
我加了一批「聰明的」hack，結果讓 client 方向陷入約 **200 輪** 反覆除錯的死結：

- **directed escan**：讓 `brcmf_escan_prep` 直接吃 `request->bssid`，想「精準只掃 GO」——
  反而觸發 driver 在 cfg80211 流程外做了一次 out-of-band association。
- **EAPOL-driven connect_done**：收到 EAPOL 就自己呼叫 `cfg80211_connect_done` ——
  這是致命錯誤：**在沒有 pending `.connect` 的情況下呼叫 `cfg80211_connect_done`，
  會破壞 cfg80211 的 SME 狀態機**，之後 cfg80211 乾脆不再把 `.connect` / `.disconnect`
  下發給 driver。表面症狀千變萬化，根本原因是這條。
- **single-channel scan pin**：把 scan 釘死單一 channel，掩蓋了真正的 off-channel 問題。

破解點來自三項堅持：**(1) 燒回 a15 做公平對照**、**(2) 回歸根本查「為何 cfg80211
不呼叫 `brcmf_cfg80211_connect`」**、**(3) 清淨重置 —— 把我加的 hack 全部拆掉、只固化診斷 print，
再用標準 `P2P_SEARCH` / escan 流程重建**。清淨重置後才發現：**這團死結是我自己製造的**，
回到標準 cfg80211 流程 + 上述針對韌體 quirk 的最小修正，client 與 GO 兩個方向一次就通。

> 教訓：mainline driver 遷移時，先問「reference driver（bcmdhd）對同顆韌體怎麼做」，
> 不要在還沒理解框架契約（cfg80211 SME）前自作聰明繞路 —— 繞路製造的假象會吃掉數倍的除錯時間。

## 4. Root Cause 分析

### 4.1 背景：P2P bsscfg 的生命週期與 cfg80211 SME 契約

先解釋後文會用到的名詞：

| 名詞 | 白話意思 |
|---|---|
| bsscfg | 韌體內一個獨立的「BSS 設定槽」。P2P device / GO / client 各自佔一個 bsscfg，透過 `p2p_ifadd`/`p2p_ifdel` 建立與拆除 |
| iovar / ioctl | driver 對韌體下的命令。有些是「全域」的、有些是「針對某個 bsscfg」的 |
| bsscfg「up」 | 這個 BSS 真的在運作：GO 在 `start_ap()` 後（`BRCMF_VIF_STATUS_AP_CREATED`）、client 在關聯成功後（`CONNECTED`）。**未 up 前韌體只接受有限命令** |
| fweh IF event | 韌體透過 event 告訴 host「某個 interface 建立了 / 刪除了」，帶 role 與 NOIF flag |
| cfg80211 SME | Linux 無線子系統的連線狀態機。`.connect`/`.disconnect` 由它下發給 driver，連線結果由 driver 透過 `cfg80211_connect_done`/`cfg80211_disconnected` 回報。**兩者必須配對** |
| WSC 兩階段 | P2P client 入群分兩段：(1) EAP-WSC（PBC）換 WPA2 憑證；(2) 用該憑證做 WPA2-PSK 4-way handshake |

一次 P2P client 入群的正常旅程：

```
[wpa_supplicant] P2P_CONNECT
   ↓ ① GO Negotiation（決定誰當 GO）→ role=client
[cfg80211] .connect（SME 下發）─── 必須真的走到 driver 的 brcmf_cfg80211_connect
   ↓ ② driver 建 P2P client bsscfg（p2p_ifadd）、留在 GO 的 channel
[fw/host] ③ EAP-WSC PBC：EAPOL 上下往返 → 拿到 WPA2 憑證
   ↓ （GO 可能 deauth 一次，wpa 用新憑證重連）
[fw/host] ④ WPA2-PSK 4-way handshake（M1..M4，buf_key_b4_m4 讓 M4 明文送出被接受）
   ↓
[host] ⑤ cfg80211_connect_done（配對 ①）→ DHCP 192.168.49.x → ping 通
```

本案的所有 bug 都落在某一步「driver 對這顆韌體，在 bsscfg 未 up 的狀態下，
下了 bcmdhd 不會下的命令」，或「host 的 data-path（EAPOL / IF event）沒有為 P2P client 的
特殊生命週期讓路」。

### 4.2 根因群組（三類）

**(A) 韌體在「未 up 的 P2P bsscfg」上對特定命令會 trap。**
BCM43752 這顆韌體對 `WLC_SET_PROMISC(cmd=10)`、mcast list、ARP/ND offload、
對 GO 的 `roam_off`/`SCB_TIMEOUT`、以及在 active 狀態的 `WLC_DOWN`、對 P2P client 的
`WLC_DISASSOC` 都會 trap。brcmfmac 的通用 STA 路徑會無差別地送這些；bcmdhd 對 P2P bsscfg
根本不送。**修正 = 讓 brcmfmac 對 P2P bsscfg 的命令集合對齊 bcmdhd**（延後或略過）。

**(B) fweh IF event 對 P2P 的語意和 STA 不同。**
韌體有時**不發** P2P device 的 IF_ADD（brcmfmac 直接逾時 fail），
又會在 WSC 階段發**假的** IF_DEL（brcmfmac 直接把 client 介面拆掉，WPA2 重連就斷）。
**修正 = P2P device 建立走 `p2p_dev` iovar 輪詢 fallback；P2P client 的 unarmed IF_DEL 視為 spurious 保留介面。**

**(C) host data-path 沒為 P2P client 的「netdev 尚未 up」讓路。**
WSC/4-way 的 EAPOL 在 P2P client netdev 還沒 `IFF_UP` 時就到，`brcmf_netif_rx` 直接丟棄 → WSC 永遠卡住。
**修正 = EAPOL（`ETH_P_PAE`）無論 netdev 是否 up 都往上送。**

（先前 §3.4 那批「directed escan / EAPOL-driven connect_done」屬於**自製的第四類**假根因，
清淨重置後已全數移除，不在正解內。）

## 5. 解決方案

分成 **8 個 per-fix patch**（kernel）+ **1 組使用者空間 conf 修改**（Android device 層）。
每個 patch 對應一個獨立韌體 quirk，帶說明性註解、可獨立 review，目標投稿 upstream。

### 5.1 Kernel 修正（brcmfmac，8 個 patch）

| # | patch（投稿標題） | 檔案 | 修正 |
|---|---|---|---|
| 1 | `wifi: brcmfmac: create the P2P device interface when the fw omits its IF event` | fweh.c / p2p.c | 韌體不發 IF_ADD 時，以 `p2p_dev` iovar 輪詢 + 手動 `brcmf_add_if` 建 P2P device 介面 |
| 2 | `wifi: brcmfmac: guard the P2P apsta interface-down bounce` | common.c / p2p.c | init 先設 `apsta=1`；`set_firmware` 判斷已設就跳過 `WLC_DOWN/UP`（避免對 active 韌體 down） |
| 3 | `wifi: brcmfmac: use escan V2 for P2P scans when supported` | p2p.c | 韌體支援時 P2P scan 改用 escan V2 |
| 4 | `wifi: brcmfmac: skip roam/SCB_TIMEOUT on a P2P GO in add_vif` | p2p.c | `add_vif` 對 GO 不送 `roam_off`/`SCB_TIMEOUT`；client 才送並補 `buf_key_b4_m4` |
| 5 | `wifi: brcmfmac: defer data-plane iovars on a not-up P2P bsscfg` | core.c / cfg80211.c | 未 up 的 P2P bsscfg 一律延後 data-plane iovar（促成 WSC 的關鍵修正） |
| 6 | `wifi: brcmfmac: P2P client connect/disconnect firmware workarounds` | cfg80211.c | P2P client connect/disconnect 韌體 workaround（跳過 DISASSOC、join 前收 off-channel、wsec_info BSS_ALGOS） |
| 7 | `wifi: brcmfmac: keep a P2P client interface across a spurious IF_DEL` | fweh.c | P2P client 的 spurious unarmed IF_DEL 保留介面 |
| 8 | `wifi: brcmfmac: deliver EAPOL to a not-yet-up P2P client netdev` | core.c | 未 up 的 P2P client netdev 也要遞送 EAPOL |

以下摘錄幾個核心修正的實際 diff。

**#5 —— 未 up 的 P2P bsscfg 延後 data-plane iovar（最關鍵）：**

```c
/* A P2P interface's bsscfg (created via p2p_ifadd) is not operational until
 * its bss comes up: a GO at start_ap() (BRCMF_VIF_STATUS_AP_CREATED), a client
 * at association (BRCMF_VIF_STATUS_CONNECTED). Data-plane iovars
 * (mcast_list/allmulti/promisc/arp/nd) sent to a not-yet-up P2P bsscfg crash
 * this firmware (BCM43752); defer them and re-apply once the bss is up.
 */
static bool brcmf_p2p_dataplane_deferred(struct brcmf_if *ifp)
{
	if (!ifp->vif)
		return false;
	if (ifp->vif->wdev.iftype == NL80211_IFTYPE_P2P_GO)
		return !test_bit(BRCMF_VIF_STATUS_AP_CREATED, &ifp->vif->sme_state);
	/* P2P client: never program the data-plane iovars; WSC/4-way/DHCP/ping
	 * need none of them, and this firmware crashes on WLC_SET_PROMISC. */
	if (ifp->vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)
		return true;
	return false;
}
```

`brcmf_configure_arp_nd_offload()` 與 `_brcmf_set_multicast_list()` 進入時先檢查此函式；
GO 則在 `start_ap()`（`AP_CREATED`）後 `schedule_work(&ifp->multicast_work)` 補做延後的設定。

**#6 —— P2P client connect/disconnect workaround（三處）：**

```c
/* (1) link_down / disconnect：P2P client 跳過 WLC_DISASSOC（會 trap，介面靠 p2p_ifdel 拆） */
if (bus_up && vif->wdev.iftype != NL80211_IFTYPE_P2P_CLIENT) {
	err = brcmf_fil_cmd_data_set(vif->ifp, BRCMF_C_DISASSOC, NULL, 0);
	...
}

/* (2) connect 進入時：先收掉 P2P device 的 off-channel 活動（scan + remain-on-channel），
 *     讓 radio 停在 GO 的 channel 上完成 join 與 WSC EAP，mirror bcmdhd */
if (ifp->vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) {
	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status))
		brcmf_abort_scanning(cfg);
	if (devif)
		brcmf_p2p_cancel_remain_on_channel(devif->ifp);
}

/* (3) 為 GC bsscfg 提交 wsec_info(BSS_ALGOS)，否則此韌體不把 EAPOL 上送 host */
if (ifp->vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) {
	u8 wi[16] = { 0x01,0,0,0x01, 0x06,0x01,0x08,0, 0,0,0,0, 0,0,0,0 };
	(void)brcmf_fil_bsscfg_data_set(ifp, "wsec_info", wi, sizeof(wi));
}
```

**#8 —— EAPOL 在 netdev 未 up 時仍上送：**

```c
if (!(ifp->ndev->flags & IFF_UP)) {
	/* Always deliver EAPOL (802.1X): WPS/4-way happens before a P2P
	 * client netdev is fully up; dropping it stalls WSC. */
	if (skb->protocol != htons(ETH_P_PAE)) {
		brcmu_pkt_buf_free_skb(skb);
		return;
	}
}
```

**#7 —— spurious IF_DEL 保留 P2P client 介面：**

```c
if (ifp && ifevent->action == BRCMF_E_IF_DEL) {
	bool armed = brcmf_cfg80211_vif_event_armed(drvr->config);
	/* fw emits a spurious IF_DEL for the P2P client bsscfg on the
	 * WSC-phase link-down; removing it breaks the WPA2 reconnect. A real
	 * teardown (del_virtual_intf) arms vif_event; only then remove. */
	if (!armed && ifevent->role == BRCMF_E_IF_ROLE_P2P_CLIENT) {
		/* spurious P2P-client IF_DEL: keep the interface */
	} else if (!armed) {
		brcmf_remove_interface(ifp, false);
	}
}
```

### 5.2 使用者空間 conf（Android device 層）

```diff
# p2p_supplicant_overlay.conf / wpa_supplicant_overlay.conf
 disable_scan_offload=1
 p2p_go_vht=1
-p2p_no_group_iface=1
+p2p_no_group_iface=0
```
```diff
# <board>/init.rc
-setprop wifi.direct.interface p2p0
+setprop wifi.direct.interface p2p-dev-wlan0
```

三項改動皆經**實測驗證為必要**（且已逐一排除不必要者）：

| 改動 | 是否必要 | 驗證 |
|---|---|---|
| `p2p_no_group_iface=1→0` | **必要** | `=1` 時 wpa 把 P2P group 跑在 `wlan0`（無專屬 group iface）→ `GROUP-REMOVED wlan0 FORMATION_FAILED`。brcmfmac 需要專屬 group vif（`p2p-wlan0-N`，由 `brcmf_p2p_add_vif` 建），bcmdhd(fullmac) 才容忍 default |
| `p2p_go_vht=1` | **保留原值**（曾誤判需移除） | clean kernel 下 GO 起 VHT80 / ch157 / 5785MHz，ping 0% loss —— 先前「導致 -52 coex」是 hack 未清時的假象 |
| `wifi.direct.interface=p2p-dev-wlan0` | **必要** | brcmfmac 建立的 P2P device 介面名為 `p2p-dev-wlan0`（a15 bcmdhd 為 `p2p0`） |
| ~~`p2p_go_intent=15`~~ | 已移除 | 永遠被 framework `selectGroupOwnerIntentIfNecessary` 或 `connect -i` 覆蓋，設了無效 |

> conf-only 改動仍需重建 `vendor.img`（brcmfmac.ko 在 `vendor_dlkm.img`，未變時不動）。

### 5.3 驗證結果（adb 實測，2026-08-26）

**方向 A — 板子當 client 連手機（`connect -i 0`）：**

```
P2P-GO-NEG-SUCCESS role=client → WPS-SUCCESS
WPA: Key negotiation completed [PTK=CCMP GTK=CCMP]
CTRL-EVENT-CONNECTED / P2P-GROUP-STARTED p2p-wlan0-0 client
DHCP → 192.168.49.28
ping 192.168.49.1 → 4 packets, 0% loss ✔
```

**方向 B — 板子當 GO 被手機連（`connect -i 15`）：**

```
P2P-GROUP-STARTED p2p-wlan0-0 GO freq=5785 (VHT80 / ch157)
AP-STA-CONNECTED <phone-mac>
手機 DHCP 取得 192.168.49.x
ping <client-ip> → 0% loss ✔
```

兩個方向皆端到端成立、可重複；WiFi Direct 驗收 = **Pass（雙向）**。

## 6. 上游狀態

- 8 個 kernel 修正已整理為乾淨的 per-fix commit（診斷 print 已全部剝除、每筆帶說明性註解），
  目標投稿 linux-wireless（brcmfmac maintainer: Arend van Spriel）。
- 每個 commit 對應一個可獨立論證的韌體 quirk，並在 commit message / 註解中標明
  「mirror bcmdhd」的依據，便於 maintainer 對照 vendor driver 行為。
- 使用者空間 conf 屬 BSP 整合，走內部 code review（不投 upstream）。

## 7. 結論與建議

1. **「同板同韌體、vendor→mainline 換 driver」時，reference driver 是最強的規格書。**
   本案每一個正解都來自「bcmdhd 對這顆韌體怎麼下命令」。遇到韌體 trap，先問
   「bcmdhd 在這一步做/不做什麼」，而不是猜時序或 RF。
2. **未 up 的 bsscfg 命令集合是 fullmac P2P 的核心陷阱。** BCM43752 韌體對「還沒 up 的
   P2P bsscfg」只接受有限命令；brcmfmac 的通用 STA 路徑會無差別下發 data-plane iovar
   （promisc/mcast/ARP-ND）與 roam/disassoc，全部要為 P2P 讓路。
3. **不要在理解框架契約前自作聰明。** 最貴的一段除錯（~200 輪）是自製 hack 造成的：
   在沒有 pending `.connect` 時呼叫 `cfg80211_connect_done` 破壞了 cfg80211 SME 狀態機。
   清淨重置（拆掉所有 hack、回標準流程）才讓真正的最小修正浮現。
4. **conf 改動要逐一驗證必要性。** `p2p_no_group_iface=0` 經實測確為 brcmfmac 必需
   （bcmdhd 不需），而 `p2p_go_intent=15` 實測無效已移除、`p2p_go_vht=1` 的負面推論被推翻 ——
   每一行都用「改回去會不會壞」驗證，不留 cargo-cult 設定。
