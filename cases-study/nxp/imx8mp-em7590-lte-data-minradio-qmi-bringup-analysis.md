# i.MX8MP LTE 行動數據打通分析（Sierra EM7590 / QMI / Google minradio Radio HAL）

## 1. 問題概述

i.MX8MP 平台（Android BSP）需要用一顆 **Sierra EM7590**（USB、data-only、Qualcomm/QMI）modem
提供 LTE 行動數據。產品驗收的最終標準是：**開機插卡即用 —— 打開 Settings「行動數據」開關就能上網，
且系統 SELinux 維持 Enforcing、零 denial**。

這件事的難點不在「讓 wwan0 拿到 IP」（那在 shell 下手動就能做到），而在於把一顆
**不含標準 RIL、廠商也沒提供 Android HAL 的 data-only modem**，一路接進 Android 的
Telephony / ConnectivityService 框架，並且在 AOSP 的 sepolicy neverallow 限制下合法落地。

整個過程分三個階段推進，每一階段解掉一層問題：

| 階段 | 目標 | 成立判準 |
|---|---|---|
| **Phase 1** | kernel 驅動 + 手動 raw-QMI 撥號 → wwan0 上網 | `shell> ping 8.8.8.8` 通 |
| **Phase 2** | 過渡：平台簽章 NetworkAgent 讓 **app 層**能用行動數據 | app-uid `ping google.com` 解析 + 通 |
| **Phase 3** | 正解：Google **minradio** Radio HAL → Settings toggle、Enforcing 零 denial | 開開關即上網、`getenforce=Enforcing`、`0 denial`、拔卡不空轉 |

典型的初期症狀：

- modem 列舉出來了（`lsusb` 有 VID 1199），但預設 composition 下**沒有 QMI 控制節點**，也撥不了號
- 手動把 wwan0 設好 IP，**封包卻完全不通**（raw-IP 模式陷阱）
- 手動 `ndc` 建網路後 IP 能通，**但 DNS 怎麼樣都設不進去** → app 解不了域名
- 導入 HAL 後 build 直接掛在 **`sepolicy_neverallows`**（HAL 想自己 `mknod` / 寫 root sysfs）
- 沒插卡開機時，`com.android.phone` **持續吃 CPU**、netId 每幾秒狂爬（data 重試風暴）

## 2. 系統環境

| 項目 | 版本 / 型號 |
|---|---|
| 平台 | i.MX8MP（客製載板，Android BSP） |
| BSP | NXP **android-16.0.0_2.0.0**，linux-imx **6.18**，GKI |
| LTE 模組 | **Sierra Wireless EM7590**（USB，VID `1199`，**data-only**，Qualcomm/QMI） |
| SIM / 電信 | 某電信商（`gsm.sim.operator.numeric` = **<SIM-MCCMNC>**） |
| 資料介面 | `wwan0`（**raw-IP**）＋ QMI 控制節點 `/dev/cdc-wdm0` |
| Radio 框架 | Google **minradio**（`hardware/interfaces/radio/aidl/minradio/`，AIDL v4） |
| 打包 | APEX `com.android.hardware.radio.minradio.virtual`（soc_specific） |

**關鍵背景 —— 為什麼一定要有 RIL：**
Settings「行動數據」toggle 的完整鏈路是
`toggle → Telephony data-enabled → RIL（IRadio HAL）→ modem`。
**沒有 RIL，Telephony 就沒有 SIM 訂閱，開關根本不會出現。**
EM7590 是 data-only（無語音）、且沒有廠商 Android HAL，所以這條 RIL 得自己補上 —— 這正是 Phase 3 的核心。

## 3. 除錯過程

### 3.1 診斷工具箱

整個案子用到的觀測 / 驗證工具：

| 層 | 工具 | 看什麼 |
|---|---|---|
| USB 列舉 | `lsusb`、`dmesg`、`/sys/class/usbmisc/`、`/sys/class/net/` | modem 有沒有出現、QMI 節點/wwan0 有沒有建 |
| modem 控制 | **AT 指令**（`ttyUSB*`）：`AT!USBCOMP`、`AT+CGPADDR`、`AT+CGCONTRDP` | 切 composition、拿本次 IP / DNS |
| QMI 撥號 | 自寫 **`qmistart2.c`**（NDK aarch64 static，raw-QMI，無 libqmi） | 直接對 `/dev/cdc-wdm0` 下 WDS Start-Network |
| 框架 | `logcat`（`RILJ`/`DataNetwork`/`ConnectivityService`/`Telephony`）、`dumpsys connectivity`、`cmd connectivity`、`ndc` | RIL 有沒有回、NetworkAgent 有沒有註冊、預設網路是誰 |
| sepolicy | `dmesg \| grep avc`、`audit2allow`、`getenforce`/`setenforce` | 抓 denial、先 permissive 收斂再 enforcing |
| 最終判準 | `ping 8.8.8.8`（IP）、`ping google.com`（DNS） | **只有 ping 通才算 PASS** |

### 3.2 Phase 1 — kernel 驅動 + 手動 raw-QMI 撥號

**目標**：先在最底層把 wwan0 弄上網，證明「硬體 + 撥號」本身可行，把框架整合的複雜度隔離出去。

**問題 ① — 預設 composition 沒有 QMI 節點。**
EM7590 出廠 composition 下沒有乾淨的 QMI 控制口。用 AT 指令切成 QMI composition：

```
AT!USBCOMP=1,1,10D      # 切 QMI composition → 重新列舉後出現 wwan0(raw-IP) + /dev/cdc-wdm0
```

**問題 ② — out-of-tree CDC 驅動撞 GKI KMI（重要坑）。**
一開始把 `cdc_ncm` / `cdc_mbim` 一起編進來，開機 load 時炸：

```
brcmfmac 之外：cdc_ncm: Unknown symbol usbnet_cdc_update_filter (err -2)
```

根因是 GKI 把 net driver 強制成 module（`=m`），而 out-of-tree module 對到的 KMI 符號在 GKI
base 沒 export。**解法：走純 QMI 路線** —— QMI 只需要 `usbnet` + `qmi_wwan`，完全用不到 cdc_ncm/cdc_mbim，
把它們移除即繞開整個 KMI 問題。

kernel 設定（`<board>.fragment`）：`USB_SERIAL_{OPTION,QUALCOMM,SIERRAWIRELESS,WWAN}`、
`USB_USBNET`、`USB_NET_QMI_WWAN`、`USB_WDM`。GKI 逼成 `=m` 的 net driver 靠
`SharedBoardConfig.mk` 的 `BOARD_VENDOR_KERNEL_MODULES` 加 **`usbnet.ko` + `qmi_wwan.ko`** 進 `vendor_dlkm`。

**問題 ③ — raw-QMI 撥號要自己刻。**
沒有 libqmi。自寫最小 raw-QMI helper `qmistart2.c`，直接對 `/dev/cdc-wdm0` 下 QMUX：

```
CTL: AllocCID(service=WDS) → 拿到 client id
WDS: Start-Network( APN TLV 0x14 = "internet", IPv4 TLV 0x2D ) → result = 0（成功）
```

實作上最容易踩的兩點：
- **loop-read 要跳過 indication**：例如 Packet-Service-Status（msgid `0x0022`，ctlflag `0x04`）是主動
  通知，不是你的 response；要比對 **自己 request 的 msgid** 才收，否則會把 indication 當答案解爆。
- **QMI session 在 modem 端持續，不靠握著 fd**：撥完可以關 fd，連線仍在；但會週期性掉線、**重撥常配到不同 IP**。

拿本次參數改用 AT（QMI composition 下 AT port = `ttyUSB3`）：
`AT+CGPADDR=1`（本次 IPv4）、`AT+CGCONTRDP=1`（DNS = `<DNS1>` / `<DNS2>`）。

**問題 ④ — 封包不通的元兇：raw-IP 模式（最隱蔽的坑）。**
wwan0 設好 IP、路由都對，`ping` 卻 100% loss。根因：

```
/sys/class/net/wwan0/qmi/raw_ip  預設 = N   （802.3 / Ethernet 框架模式）
```

EM7590 走的是 **raw-IP**，qmi_wwan 預設卻是 802.3。必須把它設成 `Y`，
且**要在介面 down 的狀態下寫**：

```sh
ip link set wwan0 down
echo Y > /sys/class/net/wwan0/qmi/raw_ip
ip link set wwan0 up
ip addr add <ip>/30 dev wwan0 ; ip route add default dev wwan0
```

**Phase 1 結果**：`shell> ping 8.8.8.8` 3/3 通。底層鏈路確認可行。

### 3.3 Phase 2 — 平台簽章 NetworkAgent（過渡，讓 app 能用）

**目標**：在還沒做 RIL 之前，先讓 **app 層**能用行動數據（給上層驗證、給 demo）。

**問題 — IP 能通，但 DNS 設不進去。**
純手動 `ndc network create` / `ndc network default set` 可以讓 IP 路由生效，
但 **DNS 沒有 shell 介面可設**：`ndc resolver` / `cmd dnsresolver` 都不吃，
DNS 只能經 `IDnsResolver` AIDL（框架內部）注入。結果 app 能 ping IP、卻解不了域名。

**解法 — 寫一個平台簽章的 in-tree app `WwanAgent` 註冊 `NetworkAgent`：**
`platform_apis: true` + `certificate: "platform"` → 拿到 signature 權限 `NETWORK_FACTORY`。
它讀 wwan0 的 IP、掛 default route、餵 DNS，交給 ConnectivityService；
CS 會自動設 netd 路由 + DnsResolver + 跑 validation + 設為 default。

**大坑 — `NET_CAPABILITY_NOT_VCN_MANAGED`。**
NetworkAgent 的 capabilities 若少了這個 flag，會出現非常反直覺的症狀：

```
網路明明 VALIDATED，卻不滿足「預設網路請求」
→ dumpsys connectivity: Active default network: none
→ app 仍解不了 DNS
```

補上 `NET_CAPABILITY_NOT_VCN_MANAGED` 後才真正成為 default network。
（另外 `RouteInfo` 要用 `(IpPrefix, (InetAddress)null, "wwan0", RTN_UNICAST)`：
3-arg 版在 SDK stub 看不到，且 null 需明確轉型才推得出多載。）

**Phase 2 結果**：app-uid `ping google.com` 解析 + 3/3 通 = **行動數據在 app 層打通**。
但這是過渡方案 —— 沒有 Settings 開關、需手動起 app，最終要被 Phase 3 取代。

### 3.4 Phase 3 — Google minradio Radio HAL（正解）

**目標**：補上真正的 RIL，讓 Settings toggle 出現並運作，且 Enforcing 零 denial。

**選型 — 為什麼是 minradio。**
AOSP-16 tree 內已有 Google `minradio`（`hardware/interfaces/radio/aidl/minradio/`），
**專為 data-only / 非標準 modem 設計**。`minradio-example/` 是可運作範例：
它假造「SIM present + LTE registered」讓 toggle 出現，並有一個**真的會被呼叫的 `setupDataCall`**，
預設用 `libnetdevice` 把一個假介面 `buried_eth0` 設成 192.168.97.2/30 當範本。

**策略（Path C）**：不重寫框架，只把範例 `setupDataCall` 的假介面**換成真 wwan0**：

```
setupDataCall()
  → qmiStartNetwork("/dev/cdc-wdm0", apn)   // Phase 1 的 raw-QMI 撥號搬進 HAL
  → setWwanRawIp("wwan0")                    // 寫 raw_ip=Y（Phase 1 的坑）
  → netdevice::setAddr4 / up                 // 設真 IP
  → 回 SetupDataCallResult{ ifname="wwan0", addresses=[真IP], dnses, gateways, type=IP, active }
```

一旦 `setupDataCall` 回傳 wwan0，**Telephony 會自動註冊 CELLULAR NetworkAgent 給
ConnectivityService**，Phase 2 手動做的那組 LinkProperties 框架自動接手 —— **WwanAgent 就可以退場**。

**最硬的一關 —— sepolicy neverallow（本案最大的架構性障礙）。**
早期直覺做法是「HAL 自己 `mknod` 建 `/dev/cdc-wdm0`、自己用 `DAC_OVERRIDE` 寫 root sysfs」。
**這條路整個報廢**，因為 build 直接掛：

```
sepolicy_neverallows FAILED:
  neverallow ... self:capability mknod;          (system/sepolicy/private/domain.te)
  neverallow ... self:capability dac_override;   (同上)
```

`CAP_MKNOD` 與 `CAP_DAC_OVERRIDE` 是 **AOSP 對所有 domain 的硬性 neverallow**
（連自訂 vendor domain 也不例外）。這逼出正確的架構觀：**HAL 是最小權限元件，
特權動作必須委派給合法持有該 cap 的 `init` / `ueventd`。** 於是把兩個特權動作各自外包：

1. **建 `/dev/cdc-wdm0` → 交給 ueventd。**
   根因藏在 `system/core/init/devices.cpp`：`else if (StartsWith(subsystem, "usb")) return;` ——
   cdc-wdm 的 subsystem 是 `usbmisc`，被這行直接略過（所以連 re-trigger `add` 都不建節點）。
   但同函式對顯式 `subsystem` 規則的判斷在它**之前**，所以在 `ueventd.nxp.rc` 補：

   ```
   subsystem usbmisc
       devname uevent_devname
   /dev/cdc-wdm*   0660   radio   radio
   ```

   ueventd 就會建 `/dev/cdc-wdm0`（label 由 `file_contexts` 給 `radio_device`）。
   **HAL 只 `waitCharNode()` 等節點出現，完全不 mknod。**

2. **寫 `raw_ip` → 交給 init chown。**
   `/sys/class/net/wwan0/qmi/raw_ip` 是 `root:root 0644`，HAL 沒權限寫。
   `init.imx8mp.rc` 在 `on property:sys.boot_completed=1` 時：

   ```
   chown radio radio /sys/class/net/wwan0/qmi/raw_ip
   chmod 0664        /sys/class/net/wwan0/qmi/raw_ip
   ```

   **`chown`/`chmod` 是 `setattr`，不是 neverallow 禁的 `{open write}`**，所以 vendor_init 合法；
   之後 HAL（group `radio`）用群組權限就能寫 `Y`，免 `DAC_OVERRIDE`。
   （init 直接**寫** generic sysfs 是被 `init.te` neverallow 的，但改屬性 setattr 可以；
   HAL 寫 sysfs 則本來就合法，hal_wifi/thermal 都這樣。）

3. **HAL 的 te 只留 neverallow-safe 權限**（`hal_radio_default.te`，新）：
   `self:capability { net_admin net_raw }`、`radio_device:chr_file rw`、
   `self:udp_socket create_socket_perms`（`SIOCSIFADDR`/`SIOCSIFFLAGS` ioctl）、`sysfs:{dir search, file rw}`。

**APN 內建 —— numeric 要對「SIM」不是「network」。**
一開始 `NO_SUITABLE_DATA_PROFILE` 不撥號。根因：APN 的 `numeric` 要對
**SIM 的 `gsm.sim.operator.numeric` = <SIM-MCCMNC>**，不是 network 端的 <NET-MCCMNC>。
且刻意用**最小版** `apns-conf.xml`（mcc=<MCC> / mnc=<MNC> / apn=internet）而非 838KB 全表 ——
全表裡同一組 mccmnc 已另有一筆 profile，兩筆 default 會讓 `DataProfileManager` 選錯。

**為省資源的收尾 —— 沒插卡不能空轉（前代平台曾遇過同類問題）。**
實機拔卡驗證確有此問題：沒 modem 卻回報 fake SIM `LOADED` →
框架每秒重建一次 metered cellular data network 撲空、**netId 狂爬（189→195 / 6s）、
`com.android.phone` 持續吃 CPU**（不是 error log，是 data 重試風暴）。修正：

- `RadioSim::getIccCardStatus`：`isModemPresent()`（查 `/sys/class/usbmisc/cdc-wdm0`）為否時回
  **`STATE_ABSENT`**（AOSP 無卡安靜路徑：無 subscription → 無 data 嘗試）。
- `service.cpp`：`setConnected` 前 poll modem 最多 5s，避免慢列舉被誤判 absent。
- **不是退 process** —— VINTF 已宣告該 HAL，退了框架會一直等它、反而 spam。一份 image 同時涵蓋有卡/沒卡。

**建置開關 `BOARD_ENABLE_LTE`（預設 true）。**
`false` 時 `<board>.mk` 不包 minradio apex（連內含的 VINTF manifest 一起消失，
無「declared but absent」spam）+ 不包 APN；`ProductConfigCommon.mk` 不 inherit telephony.mk
且設 `ro.radio.noril=yes` → 純 wifi、無 radio service。給「確定無 LTE 硬體」的機種一鍵切掉整包。

**Phase 3 結果（實機，Enforcing）**：Settings 行動數據 toggle → `setupDataCall` → 自寫 QMI 撥號 →
wwan0 真 IP（含 DNS）→ **CELLULAR NetworkAgent** →
ConnectivityService **IS_VALIDATED 預設網路** → `ping 8.8.8.8` 3/3、`ping google.com` 2/2；
`hal_radio_default` / `vendor_init` / `ueventd` **0 denial**；拔卡 → SIM ABSENT、phone idle 不空轉。

## 4. Root Cause 分析

| # | 根因 | 為什麼會咬到 | 正解方向 |
|---|---|---|---|
| A | **GKI KMI 封閉** | out-of-tree `cdc_ncm/cdc_mbim` 對到的 KMI 符號 GKI base 沒 export → load 即 `Unknown symbol` | 走純 QMI（只需 usbnet+qmi_wwan），移除 cdc 模組 |
| B | **qmi_wwan 預設 802.3** | EM7590 是 raw-IP，預設模式不符 → 封包 100% loss（IP/route 都對也不通） | `raw_ip=Y`（且介面 down 時寫） |
| C | **DNS 只能經框架注入** | shell 無介面設 DNS（`ndc resolver`/`cmd dnsresolver` 都不吃）→ 手動法 app 解不了域名 | 走 NetworkAgent（Phase 2 過渡）→ 最終走 RIL（Phase 3 正解） |
| D | **AOSP neverallow 架構** | `CAP_MKNOD`/`CAP_DAC_OVERRIDE` 對所有 domain 硬性禁止 → HAL 不能自己建節點/寫 root sysfs | 特權外包：node 給 ueventd、raw_ip 屬性給 init chown；HAL 只留 net_admin/net_raw |
| E | **fake-SIM 恆 present** | data-only modem 沒卡時仍回報 SIM LOADED → 框架 data 重試風暴、吃 CPU | 沒 modem 回 SIM ABSENT（不是退 process，避免 VINTF 等待 spam） |
| F | **APN numeric 對錯對象** | 對到 network 端而非 SIM 端的 mccmnc→ `NO_SUITABLE_DATA_PROFILE` | 內建最小 apns-conf.xml，numeric=SIM 端的 mccmnc |

**貫穿全案的一條主軸**：一顆「沒有標準 RIL 的 data-only modem」要進 Android，
最底層（撥號/介面）反而簡單，難的是**沿著框架契約一層層往上補**，
而每一層都有它自己的隱形規則（raw-IP 模式、DNS 注入路徑、neverallow、SIM 狀態語意、APN 比對）。

## 5. 解決方案（commit 對照）

分三 repo，整合分支，內部 commit 格式（`scope: subject` + 精簡 why-body + Change-Id）。

### 5.1 kernel（linux-imx）

| commit | 檔案 | 修正 |
|---|---|---|
| `configs: enable USB WWAN net drivers for EM7590 LTE` | `<board>.fragment` | option/qcserial + WDM/USBNET/QMI_WWAN（**移除 cdc_ncm/cdc_mbim**，繞 GKI KMI） |

### 5.2 device/nxp

| commit | 修正 |
|---|---|
| `<board>: install USB WWAN kernel modules to vendor` | `SharedBoardConfig.mk` `BOARD_VENDOR_KERNEL_MODULES` += `usbnet.ko` `qmi_wwan.ko` |
| `<board>: enable telephony and minradio Radio HAL for LTE data` | `<board>.mk` inherit telephony.mk + minradio apex（放板子 .mk，不放 common） |
| `<board>: create cdc-wdm node and raw_ip perms for minradio` | `ueventd.nxp.rc`（usbmisc 規則）+ `init.imx8mp.rc`（raw_ip chown）+ `hal_radio_default.te`/`vendor_init.te`/`file_contexts` |
| `<board>: add built-in APN for the LTE modem` | 最小 `apns-conf.xml`（SIM mccmnc/internet）→ `product/etc` |
| `<board>: add BOARD_ENABLE_LTE to gate the LTE stack` | build 開關，一鍵切整包 LTE |

### 5.3 hardware/interfaces（minradio）

| commit | 修正 |
|---|---|
| `minradio-example: dial the EM7590 modem over QMI for data calls` | `impl/QmiWwan.{cpp,h}`（純 raw-QMI 撥號）+ `impl/WwanLink.{cpp,h}`（`setWwanRawIp` + `isModemPresent`）+ `RadioData.cpp`（假 buried_eth0 → 真 wwan0） |
| `minradio-example: report SIM absent when the modem is missing` | `RadioSim.getIccCardStatus` 無 modem 回 STATE_ABSENT + `service.cpp` poll 5s |

> `hardware/interfaces` 原本沒 commit-msg hook（Change-Id 從 device/nxp 複製 hook 才有）；
> push 前 `.repo/manifests/default.xml` 要把它 re-point 到內部 fork（移到內部 remote block）。

### 5.4 核心程式片段

**raw-IP 切換（Phase 1 的坑，收進 HAL）：**

```cpp
// WwanLink.cpp：raw_ip 要在介面 down 時寫；node/屬性權限由 ueventd + init 事先備好
bool setWwanRawIp(const std::string& iface) {
    netdevice::down(iface);
    std::ofstream("/sys/class/net/" + iface + "/qmi/raw_ip") << "Y";
    netdevice::up(iface);
    ...
}
bool isModemPresent() { return access("/sys/class/usbmisc/cdc-wdm0", F_OK) == 0; }
```

**node 委派 ueventd（HAL 不 mknod，避開 CAP_MKNOD neverallow）：**

```
# ueventd.nxp.rc
subsystem usbmisc
    devname uevent_devname
/dev/cdc-wdm*   0660   radio   radio
```

**raw_ip 屬性委派 init chown（避開 CAP_DAC_OVERRIDE neverallow）：**

```
# init.imx8mp.rc
on property:sys.boot_completed=1
    chown radio radio /sys/class/net/wwan0/qmi/raw_ip
    chmod 0664        /sys/class/net/wwan0/qmi/raw_ip
```
