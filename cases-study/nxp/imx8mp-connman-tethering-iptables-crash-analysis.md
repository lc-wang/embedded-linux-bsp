# i.MX8MP ConnMan WiFi Tethering 崩潰分析（iptables 1.8.11 相容性問題）

## 1. 問題概述

在 i.MX8MP 平台（客製載板）的 Yocto 環境上啟用 WiFi tethering（SoftAP）時，connmand 直接崩潰：

```
$ sudo connmanctl tether wifi on <SSID> <PSK>
Wifi SSID set
Wifi passphrase set
Error enabling wifi tethering: Message recipient disconnected from message bus without replying
```

主要症狀：

- `connmanctl` 回報 D-Bus 對象**中途斷線**（不是一般的 error reply）
- connmand 被 systemd 自動重啟，`systemctl status connman` 顯示 uptime 重置
- WiFi STA 模式（連 AP）完全正常，只有 tethering 會炸
- 每次重試都在同一個點崩潰，100% 重現

## 2. 系統環境

| 項目 | 版本 / 型號 |
|---|---|
| 平台 | i.MX8MP（客製載板） |
| Yocto | walnascar（imx-6.12.34-2.1.0） |
| Kernel | linux-imx 6.12.34 |
| WiFi 模組 | AMPAK AP6275S（BCM43752, SDIO），brcmfmac driver |
| ConnMan | 1.43（poky 上游版本） |
| iptables | **1.8.11** |
| wpa_supplicant | 2.11（CONFIG_AP=y） |

## 3. 除錯過程

### 3.1 判定 connmand 有 crash

`Message recipient disconnected from message bus without replying` 代表 D-Bus 呼叫進行到一半、對象程序從 bus 上消失 —— 第一個懷疑就是 daemon 崩潰。從 dmesg 的 audit 訊息找到證據：**connmand 的 PID 在啟用 tethering 的瞬間改變**：

```
[  101.773286] audit: ... table=nat entries=0 op=xt_register pid=552 comm="connmand"
[  102.380134] audit: ... table=filter entries=0 op=xt_register pid=1328 comm="connmand"
```

PID 552 註冊 NAT 表（tethering 建立 MASQUERADE 規則的第一步）後就死了，systemd 重拉出 PID 1328。

### 3.2 排除 driver 與 wpa_supplicant

- `iw list` 確認 driver 支援 AP mode（`Supported interface modes: AP`）
- dmesg 無 firmware trap / brcmfmac 錯誤
- wpa_supplicant 編譯時 `CONFIG_AP=y` 有開

問題範圍縮小到 connmand 本身。

### 3.3 前景 debug 模式抓 backtrace

```bash
sudo systemctl stop connman
sudo /usr/sbin/connmand -n -d > /tmp/connman-debug.log 2>&1 &
sudo connmanctl tether wifi on <SSID> <PSK>
```

Log 最後一段直接點出死因：

```
connmand[1223]: src/iptables.c:__connman_iptables_append() 2 -t nat -A connman-POSTROUTING -s 192.168.0.2/24 -o eth1 -j MASQUERADE
connmand[1223]: src/iptables.c:prepare_target() target MASQUERADE
free(): invalid pointer
connmand[1223]: Aborting (signal 6) [/usr/sbin/connmand]
connmand[1223]: ++++++++ backtrace ++++++++
...
connmand[1223]: #7  0xffffac784104 in /usr/lib/libxtables.so.12
connmand[1223]: #8  0xffffac7843d0 in /usr/lib/libxtables.so.12
connmand[1223]: #9  0xffffac78a064 in /usr/lib/libxtables.so.12
connmand[1223]: #10 0xaaaaab0c9ec4 in /usr/sbin/connmand
```

崩潰點：準備 MASQUERADE target 時，`free(): invalid pointer` 發生在 **libxtables** 內。

## 4. Root Cause 分析

比對 connman 1.43 與 iptables 1.8.11 的原始碼，這是一個「library 升版改變記憶體所有權」的經典案例。

### 4.1 ConnMan 側：opts 指向靜態陣列

`src/iptables.c`：

```c
static struct option iptables_opts[] = { ... };   /* 靜態陣列 */

struct xtables_globals iptables_globals = {
        .opts      = iptables_opts,
        .orig_opts = iptables_opts,
        ...
};
```

且每次操作結束後 `reset_xtables()` 會把 `opts` 重設回這個靜態陣列。

### 4.2 iptables 側：1.8.11 改變 free 行為

iptables ≤ 1.8.10 的 `xtables_free_opts()` 會先檢查再釋放：

```c
if (opts != xt_params->orig_opts) {
        free(opts);
        ...
}
```

iptables 1.8.11 改成**無條件釋放**，且 `xtables_merge_options()` 內部會呼叫它：

```c
void xtables_free_opts(int unused)
{
        free(xt_params->opts);      /* 不再檢查是否為靜態 orig_opts */
        xt_params->opts = NULL;
}
```

### 4.3 崩潰流程

1. 啟用 tethering → connman 建立 `-t nat -A connman-POSTROUTING ... -j MASQUERADE`
2. `prepare_target()` → `xtables_merge_options()` 合併 MASQUERADE 的選項
3. libxtables 呼叫 `xtables_free_opts()` → `free(iptables_opts)`（**靜態記憶體**）
4. glibc 偵測到 `free(): invalid pointer` → SIGABRT

STA 模式不會經過 iptables 路徑，所以只有 tethering 觸發。此問題與 Debian Bug **#1112242**（connman 1.45 + BT tethering，iptables 升 1.8.11 後崩潰）為同一根因，上游（含最新 master）尚未修復。

## 5. 解決方案

### 5.1 修正 patch

讓 `xt_params->opts` 永遠是 heap 上的複本，libxtables 要 free 就讓它 free；同時保持與舊版 iptables 相容（舊版只 free 與 orig_opts 不同的指標）：

```c
static struct option *dup_orig_opts(const struct option *orig_opts)
{
        struct option *opts;
        unsigned int i;

        for (i = 0; orig_opts[i].name; i++)
                ;

        opts = g_try_malloc0(sizeof(struct option) * (i + 1));
        if (!opts)
                return NULL;

        memcpy(opts, orig_opts, sizeof(struct option) * i);

        return opts;
}

static void reset_xtables(void)
{
        ...
        if (xt_params->opts != xt_params->orig_opts)
                g_free(xt_params->opts);

        xt_params->opts = dup_orig_opts(xt_params->orig_opts);
        xt_params->option_offset = 0;
}
```

### 5.2 Yocto 整合

meta layer 加上 bbappend：

```
meta-<layer>/recipes-connectivity/connman/
├── connman_%.bbappend
└── connman/0001-iptables-Fix-crash-with-iptables-1.8.11.patch
```

```bitbake
# connman_%.bbappend
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://0001-iptables-Fix-crash-with-iptables-1.8.11.patch"
```

### 5.3 驗證結果

| 測試 | 修正前 | 修正後 |
|---|---|---|
| `connmanctl tether wifi on <SSID> <PSK>` | connmand SIGABRT，systemd 重啟 | ✔ AP 正常建立 |
| NAT / DHCP（client 連入取得 192.168.0.x） | 無法測試 | ✔ 正常 |
| 指定 5G 頻段 `tether wifi on <SSID> <PSK> 5180` | 無法測試 | ✔ 正常（ch36） |
| STA 模式回歸測試 | 正常 | ✔ 正常 |

## 6. 上游狀態

- Patch 已投稿 ConnMan 上游 mailing list（2026-07-08）：`connman@lists.linux.dev`，
  可於 archive 搜尋 subject「iptables: Fix crash with iptables >= 1.8.11」：<https://lore.kernel.org/connman/>
- 相同問題的公開回報：Debian Bug #1112242（尚未修復，workaround 為降版 iptables）

## 7. 結論與建議

1. **升級 iptables ≥ 1.8.11 的發行版，只要 connman 有用 tethering 就一定會踩到**，包含最新 connman 1.45 / master。在上游合入修正前，meta layer 需自帶 patch（或將 iptables pin 在 1.8.10）。
2. `Message recipient disconnected from message bus without replying` 這類 D-Bus 錯誤應優先懷疑 **daemon 崩潰**，用 PID 變化（audit log / `systemctl status`）快速確認，再用前景 debug 模式抓 backtrace。
3. Library 升版改變**記憶體所有權語義**（誰 malloc、誰 free）時，呼叫端傳入靜態/棧上記憶體的舊慣例就會變成地雷；套件升版驗證應涵蓋這類跨元件互動路徑（connman 的 CI 顯然沒測到 tethering × 新版 iptables 的組合）。

---

# 附錄：背景機制深入解析

以下補充理解這個 bug 所需的底層機制，依「為什麼會 abort → 選項表的設計 → 上游為什麼改 → 元件之間怎麼載入」的順序展開。

## A. 為什麼 free() 非 heap 記憶體會直接 abort

### A.1 程式的記憶體分區

```
高位址
   ┌─────────────┐
   │   stack     │ ← 區域變數
   ├─────────────┤
   │   heap      │ ← malloc/free 管理的區域
   ├─────────────┤
   │ .data/.bss  │ ← 全域變數、static 變數（程式載入時就固定）
   ├─────────────┤
   │   .text     │ ← 程式碼
   └─────────────┘
低位址
```

connman 的 `static struct option iptables_opts[]` 住在 **.data 區**，從程式啟動活到結束，不需要也不能被「釋放」。

### A.2 free() 憑什麼知道要釋放多少 —— heap 的隱藏帳本

`malloc(100)` 實際配置的不只 100 bytes，它在**回傳指標的前面**塞了一塊 metadata（chunk header），記錄大小與狀態：

```
        ┌──────────────┬─────────────────────┐
heap:   │ chunk header │   你的 100 bytes    │
        └──────────────┴─────────────────────┘
                       ↑ malloc() 回傳的指標 p
```

`free(p)` 第一件事就是往 `p` 前面讀 header，照帳本歸還。

### A.3 把靜態陣列丟給 free() 的下場

`free(iptables_opts)` 時 glibc 一樣往前讀「header」——但那裡是 .data 區的其他全域變數，讀出的大小、旗標全是垃圾值。現代 glibc 對 heap 完整性做了大量檢查（heap corruption 是資安漏洞溫床），發現位址不在 heap 管轄範圍、header 不合法，便主動呼叫 `abort()`：

```
free(): invalid pointer
Aborting (signal 6)
```

注意這**不是 segfault**，是 glibc 寧可讓程式立刻死、也不帶著損壞的 heap 繼續跑。backtrace 中 `#1~#6 libc.so.6` 那幾層就是 glibc 的檢查與 abort 流程。

## B. libxtables 選項表機制：orig_opts、opts 與 merge

### B.1 為什麼需要兩個指標

iptables 的命令列選項**不是固定的**：

```
iptables -A INPUT -p tcp -m tcp --dport 80 -j REJECT --reject-with tcp-reset
         ↑基本選項        ↑ --dport 是 libxt_tcp.so 的  ↑ --reject-with 是 libxt_REJECT.so 的
```

extension 是解析到 `-m`/`-j` 時才 dlopen 載入，選項表必須**在解析過程中重建**。因此：

| | 角色 | 記憶體 |
|---|---|---|
| `orig_opts` | 不變的母版（基本選項），每次 merge 以它為基底 | 靜態陣列，永不 free |
| `opts` | 目前的工作表 = 母版 + 已載入 extension 的選項 | merge 出來的 heap 陣列，每次 merge 換新 |

每條規則解析完要把工作表重設（下一條規則的 extension 組合不同），這正是 connman `reset_xtables()` 做的事，也是 bug 案發現場。

### B.2 xtables_merge_options() 逐段解析

`struct option` 是 `getopt_long()` 的表項（`name` = 選項名、`val` = 命中時的回傳代號），陣列以全零 entry 作結尾哨兵。

```c
struct option *xtables_merge_options(
        struct option *orig_opts,     /* 母版，單獨傳入 */
        struct option *oldopts,       /* 目前工作表（可為 NULL）*/
        const struct option *newopts, /* 剛載入的 extension 選項 */
        unsigned int *option_offset)  /* 輸出：分給此 extension 的編號區段 */
```

六個步驟：

```c
/* 1. 靠哨兵數出三張表的長度 */
for (num_oold = 0; orig_opts[num_oold].name; num_oold++);
...

/* 2. 舊工作表開頭是母版複本，跳過避免重複 */
if (oldopts != NULL) { oldopts += num_oold; num_old -= num_oold; }

/* 3. 配新表，基底永遠從母版新鮮複製 */
merge = malloc(sizeof(*mp) * (num_oold + num_old + num_new + 1));
memcpy(merge, orig_opts, sizeof(*mp) * num_oold);

/* 4. 抄新選項並平移 val（++mp 藏在迴圈步進式，等效 mp += num_new）*/
xt_params->option_offset += XT_OPTION_OFFSET_SCALE;   /* 每個 extension +256 */
memcpy(mp, newopts, sizeof(*mp) * num_new);
for (i = 0; i < num_new; ++i, ++mp)
        mp->val += *option_offset;

/* 5. 接上舊工作表的 extension 尾段（當年已平移過，原樣照抄）*/
if (oldopts != NULL) { memcpy(mp, oldopts, ...); mp += num_old; }

/* 6. 釋放舊工作表 ← 1.8.11 新增，即本 bug 的爆點；補哨兵後回傳 */
xtables_free_opts(0);
memset(mp, 0, sizeof(*mp));
return merge;
```

以上面那條指令為例，兩次 merge 後的工作表（注意**後載入的 extension 反而排前面**，但 getopt 按名字查表、歸屬按 val 區段判斷，順序不影響功能）：

```
[base: append/jump/source...] [REJECT: reject-with=513] [tcp: dport=257, sport=258]
 母版複本（每次重抄）           第二次 merge 進場（+512）   第一次 merge 進場（+256）
```

### B.3 option_offset 的編碼與解碼

每個 extension 的選項 `val` 都從 1、2、3 自編，合併會撞號，所以進場時平移到專屬區段（第一個 +256、第二個 +512…），offset 存進該 extension 的 `xt_t->option_offset`。解析時反向使用：

```
REJECT 定義          merge 平移         getopt 回傳      範圍比對                還原
reject-with val=1 → 1+512 = 513  →  c = 513  →  513 ∈ [512,768) → REJECT → 513-512=1 → 查 REJECT 選項表
```

connman `parse_xt_modules()` 的比對邏輯：

```c
if (c < (int) m->option_offset ||
    c >= (int) m->option_offset + XT_OPTION_OFFSET_SCALE)
        continue;   /* 不在此 extension 的區段，換下一個 */
xtables_option_mpcall(c, argv, invert, m, &fw);   /* 內部以 c - offset 還原查表 */
```

一個 offset 去程當平移量、回程當路由鍵＋還原鍵。

## C. iptables 1.8.11 為什麼拿掉「靜態陣列檢查」

### C.1 舊版為什麼有檢查

舊版 iptables 自己也把 `opts` 初始化成靜態的 `original_opts`，所以 `xtables_free_opts()` 必須防呆：

```c
if (opts != xt_params->orig_opts)   /* 指向靜態母版時不能 free */
        free(opts);
```

### C.2 新版的新不變量

1.8.11 開發週期在清理 memory leak（每次 merge 產生新陣列，舊的無人釋放）。修法是重構所有權規則，看 1.8.11 自己的初始化（`iptables/iptables.c:90`）：

```c
struct xtables_globals iptables_globals = {
        .orig_opts = original_opts,   /* 只設 orig_opts，.opts 留 NULL */
        ...
};
```

新不變量：**`opts` 只能是 NULL 或 heap 上的合併結果，靜態陣列永遠只放 `orig_opts`**。merge 的基底本來就直接從參數 `orig_opts` 取（B.2 步驟 3），不依賴 `opts` 保存母版；需要唯讀用表時以 `opts ?: orig_opts` fallback（getopt 只讀不 free，安全）。在此不變量下防呆檢查是死碼，`xtables_free_opts()` 遂改為無條件 `free(xt_params->opts)`，且所有 in-tree 使用者（iptables/ip6tables/arptables）在同一系列 commit 同步改寫。

### C.3 為什麼災難無聲無息地落在 connman 頭上

- libxtables 本質是 iptables 專案的**內部函式庫**，這套使用規則從未文件化；connman 是照抄 iptables **舊版**內部慣用法的 tree 外使用者
- 函式簽名全都沒變 → **ABI 不變、soname 仍是 libxtables.so.12** → 重新編譯、連結、打包全部綠燈
- 變掉的只有「誰擁有 `opts`」這個隱含約定 → 只在 runtime 走到那行 free 才引爆

三個版本的策略對照——都在回答「工作表重置時，母版段放哪」：

| 策略 | 重置後 opts 指向 | free 安全性 |
|---|---|---|
| 舊 iptables / connman | 靜態母版本尊 | 靠 `opts != orig_opts` 檢查保護 |
| iptables 1.8.11 | NULL（用時 `?: orig_opts` 借讀） | free(NULL) 合法 |
| 本文的 connman patch | 母版的 heap 複本 | free(heap) 合法，**新舊 libxtables 通吃** |

## D. connmand、libxtables 與 extension 的載入關係

### D.1 沒有東西被「覆蓋」——程序是拼裝出來的

connman 的 `src/iptables.c` 與 iptables 專案的 `iptables/iptables.c` 只是撞名，前者編進 connmand 本體，後者屬於 `/usr/sbin/iptables` 指令、與 connmand 程序無關。執行期的位址空間：

```
connmand 程序
┌────────────────────────────────┐
│ connmand 本體                  │ ← prepare_target() / reset_xtables()
├────────────────────────────────┤
│ libxtables.so.12               │ ← 啟動時由動態連結器載入（ELF NEEDED）
├────────────────────────────────┤
│ libxt_MASQUERADE.so            │ ← 解析到 -j MASQUERADE 才 dlopen
├────────────────────────────────┤
│ libc.so.6（malloc/free）       │
└────────────────────────────────┘
```

崩潰即是一次跨界呼叫：connmand 自己的程式碼把「不可 free 的位址」交給同程序內新版 libxtables 的函式。

### D.2 NEEDED（隱式連結）vs dlopen（顯式載入）

`#include <xtables.h>` 只提供編譯期型別資訊，與依賴無關。依賴鏈是：

```
#include <xtables.h>   → 編譯器知道怎麼呼叫（簽名檢查）
-lxtables（連結期）    → ELF 寫入 NEEDED: libxtables.so.12
ld-linux（每次啟動）   → 按 NEEDED 載入 .so、解析符號，全部在 main() 之前
```

用 `readelf -d connmand | grep NEEDED` 可直接看到 `libxtables.so.12` 在清單上，而 `libxt_MASQUERADE.so` 不在——後者走 dlopen：

| | NEEDED | dlopen |
|---|---|---|
| 依賴何時決定 | 連結期，寫死在 ELF | 執行期，程式自己組檔名 |
| 誰載入 | 動態連結器，main() 前自動 | 程式呼叫 dlopen() |
| 找不到 | 程式起不來 | 回傳 NULL，可自行處理 |

extension 有幾十個、用到哪個不可預知，全寫進 NEEDED 等於每次啟動全載，故走 dlopen。

### D.3 extension 的路徑怎麼組出來

libxtables `load_extension()` 自己拼路徑（`libxtables/xtables.c`）：

```c
snprintf(path, sizeof(path), "%.*s/%s%s.so", dir, *prefix, name);
```

- **目錄**：環境變數 `XTABLES_LIBDIR` > `IPTABLES_LIB_DIR` > 編譯時預設（本平台為 `/usr/lib/xtables/`）
- **前綴**：IPv4 先試 `libipt_` 再試通用 `libxt_`（IPv6 為 `libip6t_`）
- **名字**：指令裡的字串本身；命名慣例大寫為 target、小寫為 match

`-j MASQUERADE` → 試 `libipt_MASQUERADE.so`（不存在）→ `libxt_MASQUERADE.so` → `dlopen(path, RTLD_NOW)`。

### D.4 constructor 自我註冊：dlopen 之後不用 dlsym

dlopen 後 libxtables 沒有挖符號，而是回頭再查一次自己的註冊清單。玄機在 extension 的 constructor：

```c
/* libxt_MASQUERADE.c */
void _init(void)   /* dlopen 時由動態連結器自動執行 */
{
        xtables_register_target(&masquerade_tg_reg);   /* 主動報到 */
}
```

機制原理：編譯器把 constructor 函式位址放進 ELF 的 `.init_array` section（可用 `readelf -d libxt_MASQUERADE.so | grep INIT` 驗證），而執行 init 函式是 dlopen 規格的一部分：

```
dlopen(...)
  ├─ 1. mmap 映射 .so
  ├─ 2. 載入它自己的 NEEDED 依賴
  ├─ 3. 重定位（xtables_register_target 的位址在此填好）
  ├─ 4. ★ 執行 DT_INIT 與 .init_array 中的函式 ★ ← extension 在此報到
  └─ 5. 回傳 handle
```

所以 dlopen 回傳時註冊已完成，下一行就能從清單查到剛載入的 target。extension 報到時交出的資料結構，正包含選項表（`x6_options`）與 B.3 的 `option_offset` 欄位——至此 dlopen、merge、平移、範圍比對整條鏈閉合。程式啟動時 NEEDED 函式庫的初始化（C++ 全域物件建構等）也是同一機制，且依賴者的 constructor 保證晚於被依賴者執行。

## E. Patch 的記憶體生命週期：每次 reset 都 malloc，為什麼不會 leak？

`dup_orig_opts()` 每次 reset 都配置新複本，乍看沒有對應的 free——其實 free 分散在**兩個回收點**，先回看 patch 完整的樣子：

```c
static void reset_xtables(void)
{
        ...
        if (xt_params->opts != xt_params->orig_opts)
                g_free(xt_params->opts);              /* 回收點 1：先釋放手上的舊表 */

        xt_params->opts = dup_orig_opts(xt_params->orig_opts);   /* 才配新的 */
        xt_params->option_offset = 0;
}
```

### E.1 兩條路徑，各有人負責 free

**路徑 A：這輪有載入 extension（有 merge 發生）**

```
reset:  dup₁ 誕生，opts = dup₁
merge₁: libxtables 內部 free(opts) → dup₁ 回收 ✔    opts = 合併表A
merge₂: libxtables 內部 free(opts) → 表A 回收 ✔     opts = 合併表B
reset:  opts(表B) != orig → g_free → 表B 回收 ✔     opts = dup₂ 誕生
```

dup₁ 由 **libxtables 1.8.11 的 merge 順手回收**（回收點 2）——本 bug 的起因正是「新版 merge 會無條件 free 舊工作表」，patch 是順著這個行為設計的：既然它一定會 free，就給它一塊可以 free 的。

**路徑 B：這輪只用基本選項（沒有 merge）**

```
reset:  dup₁ 誕生，opts = dup₁
reset:  opts(dup₁) != orig → g_free → dup₁ 回收 ✔   opts = dup₂ 誕生
```

dup₁ 由下一次 reset 開頭的 g_free 回收。這個 `if` 原本只負責回收 merge 結果，patch 後連 dup 一起管——dup 永遠不等於 `orig_opts` 本尊，必進此 if。

### E.2 為什麼這不構成 leak

Memory leak 的定義是**無主記憶體隨時間累積**。此設計下任何時刻每個 address family 的 `opts` 恰好持有一塊存活配置（dup 或 merge 結果），每次換新前舊的必被回收，屬「一個蘿蔔一個坑」的穩態，長時間運行記憶體佔用不增長。程序結束時手上的最後一塊未顯式釋放（valgrind 列為 *still reachable* 而非 *definitely lost*），由 OS 於程序退出時回收，為 daemon 類程式慣例。

### E.3 Corner cases

- **merge 失敗回傳 NULL**：connman 錯誤路徑使 `opts = NULL`，下次 reset 的 `g_free(NULL)` 合法無事，隨後配新 dup，狀態自我修復。
- **跑在舊版 iptables（≤ 1.8.10）**：舊版 free 帶防呆檢查 `opts != orig_opts`，dup 同樣滿足條件、照樣被回收，新舊版本皆不漏。

Review 這類「所有權轉移」patch 時，「每一次 malloc 的對應 free 在哪」是該問的第一個問題；本例的答案較隱蔽，因為其中一個 free 藏在**別人的函式庫裡**——而這正是整個 bug 的主題：跨元件的記憶體所有權。Commit message 中 "so that libxtables is free to release and replace it" 講的就是這件事。
