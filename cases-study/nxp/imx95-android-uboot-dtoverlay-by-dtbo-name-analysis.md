# i.MX95 Android U-Boot 依 dtbo 名稱套用 device tree overlay（與 Yocto 用同一組名稱與 env）

## 1. 問題概述

一片 i.MX95 SMARC 自製板移植到 Android 16 時，要讓 Android 和 Yocto 一樣，用 device tree overlay 選擇周邊：MIPI-CSI 相機（OV5640）、LVDS 面板（G121XCE-L01）、HDMI 4K（LT9611UXD 的 4K DPU clock）。

Android 的 `boota` 不會從檔案系統讀 `.dtbo`，overlay 只能從 AVB 驗證過的 **dtbo partition**（`dtbo.img`，Android 的 dt_table 格式）裡拿。前一個 i.MX8MP 專案已經在 U-Boot 加了一套做法，這次一開始直接沿用：

- `dtoverlay` env 放一個短名稱（例如 `ov5640`），U-Boot 用 `strcmp` 把短名稱轉成一個 enum 值。
- **enum 值直接當作 `dtbo.img` 裡的 entry 位置**（`[0]` 是 base dtb，`[1]` 是第一個 overlay……）。
- overlay 套用成功後，視需要在 `/firmware/android` 下寫一個屬性（例如 `camera.layout`），Android init 會匯入成 `ro.boot.*`。

這個做法能動，但 code review 時暴露出幾個結構性問題：

1. **enum 值必須等於 BoardConfig.mk 裡 dtbo 的順序。** 兩邊都是手動維護，順序一改就套錯 overlay。
2. **沒有邊界檢查。** U-Boot 用 enum 值直接算出 entry 位址；如果 `dtbo.img` 是舊版、entry 比較少，會讀到 table 外面的垃圾，再拿垃圾的 `dt_size`/`dt_offset` 做 `memcpy`。
3. **enum 無法分組。** 相機、面板混在同一組編號裡（camera 1、LVDS 2，之後新相機 3、新面板 4……），而且因為等於位置，不能跳號。
4. **Android 和 Yocto 名稱、指令都不一樣。** Yocto 用 `boot_fdt_overlay=yes` + `fdt_overlay_files=<dtbo 檔名>`，Android 用 `dtoverlay=<自訂短名>`，同一塊板子要記兩套。
5. 原本的 code 還有一個隱藏的 stack overflow（見 3.1）。

最後的做法：把 overlay 處理獨立成 `fb_android_dtbo.c`；**build 時把每個 overlay entry 的 `id` 欄位設成 dtbo 檔名的 crc32，U-Boot 開機時把 env 裡的名稱算 crc32 去比對 `id`**。名稱、env 變數、預設值都跟 Yocto 一樣，不需要任何手動維護的編號或順序。

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55） |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP android-16.0.0_2.0.0 |
| U-Boot | 2026.04（`vendor/nxp-opensource/uboot-imx`，Android 和 Yocto 共用同一個自家分支） |
| Kernel | 6.18（Android kernel，overlay 的 `.dtso` 與 Yocto 相同） |
| 開機 | SD 卡開機，`boota`（boot header v4，`vendor_boot` + `dtbo` partition） |
| dtbo partition | `CONFIG_INCLUDE_DTB_TO_VENDOR_BOOT` 沒開，所以 `dtbo.img` 的 entry `[0]` 是 base dtb，overlay 從 `[1]` 開始 |
| Android 端改 env | libubootenv `fw_setenv`（`fw_env.config` 指向 SD 卡上 U-Boot env 的位置） |

用到的 overlay（檔名與 Yocto 相同）：

| dtbo | 作用 | Android 需要的附加屬性 |
|---|---|---|
| `imx95-smarc-ov5640.dtbo` | 開 MIPI-CSI OV5640 相機 | `camera.layout=ov5640`（相機 HAL 依此選設定檔） |
| `imx95-smarc-lvds-g121xcel01.dtbo` | 開 LVDS 面板、背光 | `lcd_density=240` |
| `imx95-smarc-mipi2hdmi-lt9611uxd-4k.dtbo` | DPU pixel clock 改 297 MHz（4K30） | `displaymode=4kp30`、`lcd_density=640` |

## 3. 過程

### 3.1 沿用舊做法：enum + 位置

先把 OV5640 照舊做法接上。讀舊 code 時發現一個從舊專案帶過來的 bug：

```c
char overlay_name[] = {0};              /* 只有 1 byte */
...
overlay_name_ptr = env_get("dtoverlay");
sprintf(overlay_name, "%s", overlay_name_ptr);
```

`overlay_name` 只有 1 byte，`sprintf` 每次開機都會寫爆 stack。即使沒設 `dtoverlay`，也會寫入 `"(null)"` 這 7 個 byte。另外 `fdt_increase_size()`、`fdt_overlay_apply()` 的回傳值沒有接，後面的 `if (!ret)` 判斷的是舊值。第一個修正改成 128 bytes + `strlcpy`，並接上回傳值。

接著加上 OV5640：

```c
enum overlay_type {
	NO_OVERLAY = 0,
#if defined(CONFIG_TARGET_<BOARD_B>)
	DSI_10_TQ101AJ02_8MP = 1,
#endif
#if defined(CONFIG_TARGET_<BOARD_A>)
	CAM_OV5640_95 = 1,
#endif
};
...
dt_entry_overlay = (struct dt_table_entry *)((ulong)dt_img +
	be32_to_cpu(dt_img->dt_entries_offset) +
	(u32)dtbo_idx * be32_to_cpu(dt_img->dt_entry_size));   /* enum 直接當位置 */
```

套用成功後在 `/firmware/android` 寫入 `camera.layout = "ov5640"`。板上驗證：

| 狀態 | 結果 |
|---|---|
| 沒設 dtoverlay | 開機正常，沒有相機，也沒有 `ro.boot.camera.layout` |
| `fw_setenv dtoverlay ov5640` 後重開 | U-Boot 印出 `ANDROID: fdt overlay OK`，fdt 從 96941 變成 100328 bytes；ov5640 bind 上；`ro.boot.camera.layout=ov5640`，kernel cmdline 裡沒有這個參數 |
| Camera2 app | 預覽約 31 fps，拍照 1920×1080 JPG 正常 |

review 意見（AI reviewer）指出 `fdt_increase_size`、`fdt_path_offset`、`fdt_setprop_string` 失敗時沒有 log，PS2 補上每一步的回傳值檢查與 `ANDROID: set camera.layout failed, ret=%d`。

LVDS 面板也用同樣方式加進來（`LVDS_G121XCEL01_95 = 2`，對應 `dtbo.img` 的 `[2]`），之後再加上 `lcd_density=240`。

### 3.2 review 意見

LVDS 這個 commit 的第一版收到兩則意見：

| 來源 | 意見 |
|---|---|
| AI reviewer | `dtbo_idx` 沒和 `dt_img->dt_entry_count` 比對。搭配只有 2 個 entry 的舊 `dtbo.img`，索引 2 會越界讀 table，`dt_size`/`dt_offset` 拿到垃圾後 `memcpy` 會讓開機掛掉。 |
| 主管 | display 和 camera 要分別集中，例如 camera 從 1 開始、display 從 101 開始；之後還會移植其他相機，編號會混在一起。 |

### 3.3 第一版修正：entry id + 分組編號（沒有推）

先試了一個最小改動的版本：

- `dtbo.mk` 的 `TARGET_BOARD_DTBO_CONFIG` 多一個可選的第三欄當 id（`imx95-smarc-ov5640.dtbo:1`、`imx95-smarc-lvds-g121xcel01.dtbo:101`），傳給 `mkdtimg --id=`。
- U-Boot 的 `find_dtbo_entry()` 在 entry table 裡找 `id` 相等的 entry；沒有 id 的舊 image 才退回位置查找，並檢查 `dt_entry_count`。

這版解決了越界和分組，但 **enum 和 BoardConfig.mk 的數字仍然要手動配對**，只是從「第幾行」變成「冒號後面的數字」。這版沒有送 review。

### 3.4 改成名稱比對（參考 TechNexion a54aad0）

主管建議參考 TechNexion 的 `fb_fsl: Android dtoverlay extention`（a54aad0），要點：

1. overlay 處理獨立成一個檔案，不要再污染 `fb_fsl_boot.c`；
2. 用字串比對取代 enum；
3. **名稱比照 Yocto**：Yocto 怎麼下 overlay，Android 就用一樣的名稱，不會被 enum 編號綁死，U-Boot 的指令也希望一致。

讀過 TechNexion 的 commit 後，結構可以直接採用，但它本身還有幾個問題：

| TechNexion a54aad0 | 問題 |
|---|---|
| 「名稱在表格中的位置 + 1」直接當 dtbo slot，註解寫 `Sync the order of _ext_dtbo with BoardConfig.mk` | 順序耦合還在，只是從 enum 換成表格順序 |
| `dt_entry_count` 的檢查被註解掉 | 越界問題還在 |
| `char dtoverlay[32]` + `sprintf` | 和 3.1 修的是同一類 overflow |
| `fdt_overlay_size < 0`（`u32`） | 永遠不成立 |

所以採用它的結構（獨立檔案、每板一張附加屬性表、字串名稱），把「名稱 → entry」這一步改成 crc32 比對 entry `id`。

### 3.5 後續 review 與修正

| 版本 | 意見 | 處理 |
|---|---|---|
| 名稱比對第 2 版 | 舊短名 `ov5640` 沒保留，已經設好 `dtoverlay=ov5640` 的板子升級後 overlay 和 `camera.layout` 會靜默消失；附加屬性要用解析後的 dtbo 名稱比對 | 加 alias 表：短名對到完整 dtbo 名稱，仍走 crc32；只有整個 dtbo 都沒有 id（舊 image）才退回舊 index，並檢查 entry 數 |
| 名稱比對第 2 版（主管） | 移除 `fb_fsl_boot.c` 舊 code 獨立成一個 commit，下一版 Android U-Boot 就不用再上那些 commit；commit message 註明參考來源 | 拆出一個 commit 把 `fb_fsl_boot.c` 還原成 NXP 版本，名稱比對的 commit message 加上 TechNexion commit 和 `Link:` |
| 名稱比對第 3、4 版 | **[blocking]** `fdt_overlay_apply()` 失敗後 base fdt 處於未定義狀態，`continue` 會拿壞掉的 DT 繼續開機；`dt_size` 沒驗證就 `memcpy` 到 `fdt_addr+0xF0000`，fdt 長大後也沒檢查會不會撞到 | overlay 改複製到 malloc buffer；檢查 offset/size 在 image 內、header 合法；fdt 成長上限 0xF0000；套用前備份 fdt，失敗就還原並跳過（第 5 版） |
| 還原 `fb_fsl_boot.c` 的 commit | commit message 說「保留呼叫」但實際整段刪光，單獨這個 commit dtoverlay 會失效 | commit message 改成明寫 `dtoverlay does not work at this commit alone` |
| `dtbo.mk` 第 1 版（主管） | 想確認是否只能用 python 算 crc32，mkdtimg 是否有參數能做到；`.mk` 原本都是 shell，混 python 有點亂 | 改用 gzip trailer 取 crc32（見 5.2），拿掉 py3-cmd |

第 5 版的失敗路徑（overlay 套到一半破壞 fdt、截斷的 dtbo、offset 超出 image、fdt 超過上限、`dt_size` 小於 header）在 build server 上用 U-Boot 自己的 libfdt 寫 host test（開 AddressSanitizer / UBSan）驗證；板上因為 dtbo partition 有 AVB 保護，這些錯誤無法直接觸發。

### 3.6 4K overlay 的附加屬性

接上 4K 螢幕後，4K overlay 雖然套用了，Android 還是跑 1920×1080，而且版面變成平板樣式。原因有兩個：

- overlay 把 DPU pixel clock 設成 297 MHz，螢幕只拿得到 3840×2160@30。沒有 `displaymode` 時，HWC 退回 1080p60，並把 30 Hz 的 mode 排除。
- density 維持 320 時，4K 的寬度是 1080dp，進入平板版面。

用同一張附加屬性表加兩筆：套用 4K overlay 時寫入 `displaymode=4kp30`、`lcd_density=640`。之後只要設好 overlay，Android 就會自己用 3840×2160@30、density 640 開機。

## 4. 設計分析

### 4.1 背景與術語

| 術語 | 白話 |
|---|---|
| dtbo partition / `dtbo.img` | Android 放 device tree（overlay）的 partition。內容是 dt_table 格式，由 `mkdtimg` 產生，尾端有 AVB hash footer，由 vbmeta 驗證 |
| `dt_table_header` | `dtbo.img` 的檔頭：`total_size`、`dt_entry_count`、`dt_entries_offset`、`dt_entry_size` 等，全部 big-endian |
| `dt_table_entry` | 每個 dtb/dtbo 一筆：`dt_size`、`dt_offset`、`id`、`rev`、`custom[4]`。`id`/`rev` 是 Android 格式定義的可選欄位（「must be zero if unused」），原本是讓 bootloader 依硬體 id 選 dtb |
| `boota` | NXP U-Boot 的 Android 開機指令（`drivers/fastboot/fb_fsl/fb_fsl_boot.c` 的 `do_boota()`），載入 boot/vendor_boot/dtbo，把 base dtb 放到 `fdt_addr` 後開 kernel |
| `/firmware/android` | DT 裡的節點。Android init 會把這個節點下的屬性匯入成 `ro.boot.<名稱>`，效果等同 cmdline 的 `androidboot.<名稱>=` |
| androidboot 屬性 | `ro.boot.camera.layout`（相機 HAL 選設定）、`ro.boot.lcd_density`（板子 init.rc 設 density）、`ro.boot.displaymode`（HWC 選解析度） |
| `boot_fdt_overlay` / `fdt_overlay_files` | Yocto 的 U-Boot env：前者是開關，後者是用空白分隔的 dtbo 檔名清單 |

Yocto 的 U-Boot env 怎麼載 overlay（`board/<vendor>/<board>/<board>.env`）：

```text
loadoverlay=if test ${boot_fdt_overlay} = yes; then
		...
		for overlay_file in ${fdt_overlay_files}; do
			fatload mmc ${mmcdev}:${mmcpart} ${fdt_ovaddr} ${overlay_file} && fdt apply ${fdt_ovaddr};
```

Yocto 從 boot partition 用檔名讀檔；Android 不能讀檔，只能用 `dtbo.img` 裡的 entry。所以問題變成：**怎麼從一個檔名，找到 `dtbo.img` 裡對應的那個 entry**。

### 4.2 為什麼用 crc32 當 entry id

`dt_table_entry` 沒有存檔名，可用的欄位只有 32-bit 的 `id`、`rev` 和 `custom[4]`。有幾個選項：

| 做法 | 評估 |
|---|---|
| enum = 位置（原本） | 兩邊手動配對，順序耦合、無法分組、要另外加邊界檢查 |
| enum = 手動 id（3.3 的版本） | 不受順序影響，但數字仍要兩邊配對 |
| 把檔名塞進 `custom[4]`（16 bytes） | 檔名（如 `imx95-smarc-mipi2hdmi-lt9611uxd-4k`）超過 16 bytes，放不下 |
| 在每個 dtso 加一個 id property，`mkdtimg --id=/:prop` 讀出來 | 每個 overlay 都要寫死一個 hash 值，而且要改 kernel 的 dtso |
| **id = crc32(dtbo 檔名，不含 `.dtbo`)** | build 端和 U-Boot 端都從「名稱」自動算出 id，不需要任何人工編號 |

選 crc32 的理由：

1. **兩邊都有現成的實作。** U-Boot 有 `crc32()`（`u-boot/crc.h`）；build 端可以用 gzip trailer 算出同一個值（IEEE CRC-32，和 zlib、U-Boot 相同）。
2. **用的是格式本來就有的欄位。** `mkdtimg create` 本來就支援 `--id=`，NXP 自己在 `INCLUDE_DTB_TO_VENDOR_BOOT` 那條路徑也用 `--id` 編號 dtb。不改格式、不改工具。
3. **新增 overlay 只要在 BoardConfig.mk 加一行。** 只有需要附加 Android 屬性時，U-Boot 才要在表格加一筆。順序不影響結果。
4. **查找只在 `dt_entry_count` 範圍內做，找不到就跳過。** 越界問題從結構上消失。
5. **base dtb 的 id 維持 0**，U-Boot 從 entry 1 開始找，不會把 base 當 overlay。

限制（分析，非實測）：crc32 是 32-bit hash，理論上兩個不同檔名可能撞到同一個值。目前一塊板子只有個位數的 overlay，機率極低；若擔心，可以在 build 時檢查同一個 `dtbo.img` 裡的 id 沒有重複。這個檢查目前沒有做。

## 5. 實作

### 5.1 U-Boot：`drivers/fastboot/fb_fsl/fb_android_dtbo.c`

`fb_fsl_boot.c` 只加一個 include 和一個呼叫，位置在 combine cmdline 之後、顯示 unlock 警告之前：

```c
#ifdef CONFIG_OF_LIBFDT_OVERLAY
	android_apply_dtoverlay(dt_img, fdt_addr);
#endif
```

Makefile：

```make
obj-$(CONFIG_OF_LIBFDT_OVERLAY) += fb_android_dtbo.o
```

每板的附加屬性表與短名 alias：

```c
static const struct dtoverlay_prop dtoverlay_props[] = {
#if defined(CONFIG_TARGET_<BOARD_A>)
	{ "imx95-smarc-ov5640", "camera.layout", "ov5640" },
	{ "imx95-smarc-lvds-g121xcel01", "lcd_density", "240" },
	{ "imx95-smarc-mipi2hdmi-lt9611uxd-4k", "displaymode", "4kp30" },
	{ "imx95-smarc-mipi2hdmi-lt9611uxd-4k", "lcd_density", "640" },
#endif
	{ NULL, NULL, NULL }
};

static const struct dtoverlay_alias dtoverlay_aliases[] = {
#if defined(CONFIG_TARGET_<BOARD_B>)
	{ "dsi-tq101aj02", "imx8mp-<board>-mipi-dsi-tq101aj02", 1 },
#endif
#if defined(CONFIG_TARGET_<BOARD_A>)
	{ "ov5640", "imx95-smarc-ov5640", 1 },
#endif
	{ NULL, NULL, 0 }
};
```

依名稱找 entry：

```c
/* The build sets each overlay entry id to the crc32 of its dtbo name */
static struct dt_table_entry *find_dtbo(struct dt_table_header *dt_img,
					const char *dtbo)
{
	u32 count = be32_to_cpu(dt_img->dt_entry_count);
	u32 id = crc32(0, (const unsigned char *)dtbo, strlen(dtbo));
	u32 i;

	for (i = 1; i < count; i++) {
		if (be32_to_cpu(dtbo_entry(dt_img, i)->id) == id)
			return dtbo_entry(dt_img, i);
	}

	return NULL;
}
```

套用單一 overlay（範圍檢查、malloc 複製、失敗還原）：

```c
static int apply_dtbo(void *fdt, struct dt_table_header *dt_img,
		      struct dt_table_entry *entry)
{
	...
	if (size < sizeof(struct fdt_header) || offset > img_size ||
	    size > img_size - offset)
		return -FDT_ERR_BADOFFSET;

	dtbo = malloc(size);
	backup = malloc(fdt_size);
	...
	memcpy(dtbo, (void *)((ulong)dt_img + offset), size);
	ret = fdt_check_header(dtbo);
	if (!ret && fdt_totalsize(dtbo) > size)
		ret = -FDT_ERR_TRUNCATED;
	if (ret)
		goto out;

	memcpy(backup, fdt, fdt_size);
	ret = fdt_grow(fdt, size);            /* 上限 FDT_MAX_SIZE = 0xF0000 */
	if (!ret)
		ret = fdt_overlay_apply(fdt, dtbo);
	if (ret)
		memcpy(fdt, backup, fdt_size);  /* 失敗就還原 base fdt */
	...
}
```

主流程 `android_apply_dtoverlay()`：

1. `boot_fdt_overlay=yes` 時讀 `fdt_overlay_files`，再接上舊的 `dtoverlay`（相容舊板子），合併成一個清單（buffer 256 bytes，`strlcpy`/`strlcat`）。
2. 逐一處理名稱：去掉結尾的 `.dtbo`；`no_overlay` 直接略過；查 alias 表把短名換成完整 dtbo 名稱。
3. `find_dtbo()` 用 crc32 找 entry；找不到、且是 alias、且整個 dtbo 都沒有 id（舊 image）時，才用 alias 的舊 index，並檢查 `< dt_entry_count`。
4. 還是找不到就印 `ANDROID: dtoverlay <name> not found, skip`，繼續下一個。
5. 同一個 entry 只套一次（最多 16 個）。
6. 套用成功印 `ANDROID: dtoverlay <name> applied`，再依**解析後的 dtbo 名稱**查附加屬性表，寫到 `/firmware/android`。

順序：先 `fdt_overlay_files`（依清單順序），再 `dtoverlay`。

### 5.2 Build 端：`device/nxp/common/build/dtbo.mk`

```make
	for dtboplat in $(TARGET_BOARD_DTBO_CONFIG); do \
			DTBO_FILE=`echo $$dtboplat | cut -d':' -f2`; \
			DTBO_APPEND+=`echo "$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/arch/$(TARGET_KERNEL_ARCH)/boot/dts/$(DTS_OVERLAY_PATH)/$${DTBO_FILE} "`; \
			DTBO_ID=0x`printf %s $${DTBO_FILE%.dtbo} | gzip -c | tail -c 8 | head -c 4 | od -An -tx4 | tr -d ' '`; \
			DTBO_APPEND+="--id=$$DTBO_ID "; \
	done; \
```

- gzip 檔案的最後 8 bytes 是 `CRC32（4 bytes，little-endian）+ 原始長度`，`tail -c 8 | head -c 4` 取出 CRC32。這是 IEEE CRC-32，和 zlib、U-Boot `crc32()` 是同一個演算法。
- `od -tx4` 依 host 的 byte order 讀；gzip trailer 是 little-endian，build host 是 x86（little-endian），所以結果正確。
- base dtb 沒有 `--id`，`mkdtimg` 預設 0。
- smarc 的 `BoardConfig.mk` 不用改，只列 dtbo 檔名：

```make
TARGET_BOARD_DTBO_CONFIG := imx95:imx95-smarc-ov5640.dtbo
TARGET_BOARD_DTBO_CONFIG += imx95:imx95-smarc-lvds-g121xcel01.dtbo
TARGET_BOARD_DTBO_CONFIG += imx95:imx95-smarc-mipi2hdmi-lt9611uxd-4k.dtbo
```

產生的 `dtbo.img`（`mkdtimg dump`）：

| entry | 內容 | id |
|---|---|---|
| 0 | base dtb | `0x00000000` |
| 1 | imx95-smarc-ov5640 | `0xf935797e` |
| 2 | imx95-smarc-lvds-g121xcel01 | `0x4c9bc89e` |
| 3 | imx95-smarc-mipi2hdmi-lt9611uxd-4k | `0x46205955` |

PS1 原本用 python 的 `zlib.crc32`。Android build 在 Kati recipe 裡禁止直接呼叫 `python3`（`paths/config.go` 列為 Forbidden），所以 PS1 用的是 `prebuilts/build-tools/$(HOST_PREBUILT_TAG)/bin/py3-cmd`。review 時被問「能不能不用 python、mkdtimg 有沒有參數可以做」，查過之後：

- `mkdtimg` 的 `--id`、`--custom0~3` 只接受數字，或從 dtb 讀一個 u32 property（`/:prop`），**沒有依檔名算 hash 的功能**。
- 改用 shell：`gzip/head/tail/od/printf` 都由 build PATH 上的 prebuilt toybox 提供，仍然 hermetic。
- PS2 算出的四個 id 和 python 版完全相同。`dtbo.img` 整個檔案的 md5 每次 build 都會變，是因為 avbtool 的 hash footer 每次用隨機 salt，不是內容改變。

### 5.3 共用 env 預設值

`board/<vendor>/<board>/<board>.env` 把 Yocto 的兩行預設值從 Yocto 專用區段移到共用區段（Yocto 產生的 env 不變）：

```text
#include <env/nxp/imx_env.env>

boot_fdt_overlay=no
fdt_overlay_files=imx95-smarc-mipi2hdmi-lt9611uxd-4k.dtbo

#ifdef CONFIG_ANDROID_SUPPORT
...
```

### 5.4 相關 commit

| Repo | 內容 |
|---|---|
| U-Boot | 修 `overlay_name` 1-byte buffer overflow |
| U-Boot | OV5640 dtoverlay（enum 版，之後被移除） |
| device/nxp | 打包 OV5640 dtbo、LVDS dtbo |
| U-Boot | `fb_fsl_boot.c` 移除 dtoverlay 處理，還原成 NXP 版本 |
| U-Boot | 新增 `fb_android_dtbo.c`，依 dtbo 名稱查找 |
| U-Boot | LVDS overlay → `lcd_density=240` |
| U-Boot | 板子的 `.env` 與 Android 共用 overlay 預設值 |
| device/nxp | `dtbo.mk`：overlay entry id = crc32(dtbo 名稱)（shell 版） |
| device/nxp | 打包 LT9611UXD 4K dtbo |
| U-Boot | 4K overlay → `displaymode=4kp30`、`lcd_density=640` |

U-Boot 的「移除舊 code、名稱查找、LVDS density、env 預設值」四個 commit，要和 device/nxp 的 `dtbo.mk`、4K dtbo 兩個 commit 一起 merge。只 merge U-Boot 時，`dtbo.img` 沒有 id，完整名稱都找不到（會 skip，不會當機），只有短名 alias 透過舊 index 還能用。

### 5.5 考慮過但沒採用的做法

| 做法 | 問題 |
|---|---|
| 維持 enum = 位置，只補邊界檢查 | 順序耦合、編號無法分組，Android 和 Yocto 名稱不同 |
| enum + 手動 id（1 / 101，3.3） | 解決越界與分組，但 U-Boot 和 BoardConfig.mk 的數字仍要手動配對；被名稱比對取代，沒有推 |
| 直接採用 TechNexion a54aad0 | 用表格順序當 slot，仍是順序耦合；沒有 `dt_entry_count` 檢查；`char[32]` + `sprintf` |
| `mkdtimg` 的 cfg 檔或參數算 id | `--id`/`--custom` 只接受數字或 dtb 內的 u32 property，不能依檔名算 hash |
| 每個 dtso 加 id property | 要改 kernel 的 overlay，hash 值寫死在 dtso 裡，難維護 |
| 不用 overlay，把 ov5640 併進預設 dtb、`camera.layout` 寫死在 bootargs | 最快，但沒接相機的板子也會帶這個設定，也無法切換面板 |
| 用 `append_bootargs` 帶 `androidboot.*` | 屬於手動設定，重燒 SD 卡就消失；附加屬性應該跟著 overlay 一起出現 |
| 照舊專案用 env script（`panelargs`）依 `dtoverlay` 值組 bootargs | 這塊板子的 U-Boot env 和 Yocto 共用，不想在 env 裡加 Android 專用的 script；改成寫 `/firmware/android` |

### 5.6 驗證結果

| 測試 | env | 結果 |
|---|---|---|
| 完整 Yocto 名稱 | `dtoverlay=imx95-smarc-ov5640 imx95-smarc-lvds-g121xcel01` | 兩個都 `applied`；LVDS 亮、ov5640 bind、density 240、`camera.layout=ov5640` |
| `.dtbo` 後綴 + 未知名稱 | `dtoverlay=imx95-smarc-ov5640.dtbo foo` | ov5640 `applied`，`foo not found, skip`；正常開機，density 320 |
| 舊短名（alias 之前） | `dtoverlay=ov5640` | `not found, skip`，正常開機（PS4 之後由 alias 對到 imx95-smarc-ov5640） |
| 重燒 SD 卡後的預設 env | — | `boot_fdt_overlay=no`、`fdt_overlay_files=…lt9611uxd-4k.dtbo`；不套任何 overlay，只有 HDMI |
| 只開 `boot_fdt_overlay=yes` | Yocto 預設清單 | 4K overlay `applied`；live DT 的 DPU `assigned-clock-rates` 為 `0 / 2673000000 / 297000000`；1080p 螢幕仍正常 1920×1080@60 |
| 三個一起 | `fdt_overlay_files="imx95-smarc-ov5640.dtbo imx95-smarc-lvds-g121xcel01.dtbo imx95-smarc-mipi2hdmi-lt9611uxd-4k.dtbo"` | 依序全部套用；LVDS 1024×768 與 HDMI 都有畫面，density 240，`camera.layout=ov5640`；最終 fdt 103539 bytes（上限 0xF0000） |
| `dtbo.mk` shell 版 crc32 | 同上 | 四個 id 與 python 版相同，三個 overlay 都找得到、都套用 |
| 4K 螢幕（4K 附加屬性） | 只有 `boot_fdt_overlay=yes` + 4K 清單，`append_bootargs` 清空 | `ro.boot.displaymode=4kp30`、`ro.boot.lcd_density=640`；HWC 選 3840×2160@30，pixel clock 297 MHz；版面與 1080p 相同；4K H.265 影片用硬體解碼器播放正常 |
| 失敗路徑 | host test（U-Boot libfdt + ASan/UBSan） | 套到一半破壞 fdt 後可 byte-for-byte 還原；截斷、越界、超過上限、header 太小都被擋下 |
| 舊專案（i.MX8MP）編譯 | i.MX8MP 自製板的 Android defconfig | `fb_android_dtbo.o`、`fb_fsl_boot.o` 以 `-Werror` 編譯通過；**沒有在實板上驗證 DSI 面板** |

## 6. 與 Yocto 的一致性

| 項目 | Yocto | Android（改完後） |
|---|---|---|
| env 變數 | `boot_fdt_overlay` + `fdt_overlay_files` | 相同（另外保留 `dtoverlay` 相容舊板子） |
| 預設值 | `boot_fdt_overlay=no`、`fdt_overlay_files=…lt9611uxd-4k.dtbo` | 相同（共用同一份 `.env`） |
| 名稱格式 | dtbo 檔名，含 `.dtbo` | 相同；`.dtbo` 可省略 |
| 多個 overlay | 空白分隔，依清單順序 | 相同；`dtoverlay` 的項目排在後面；重複的只套一次 |
| 列了但不存在的 overlay | `fatload` 失敗、跳過、繼續開機 | `not found, skip`，繼續開機 |
| overlay 來源 | boot partition 上任何 `.dtbo` 檔 | 只有打包進 `dtbo.img` 的（BoardConfig.mk 列的） |
| 驗證 | secure boot 時檢查 `.cntr` 簽章 | 整個 dtbo partition 由 AVB 驗證 |
| Android 附加設定 | 無 | 依 dtbo 名稱寫 `/firmware/android`（camera.layout、lcd_density、displaymode） |

同一組指令兩邊通用：

```text
setenv boot_fdt_overlay yes
setenv fdt_overlay_files "imx95-smarc-ov5640.dtbo imx95-smarc-lvds-g121xcel01.dtbo"
saveenv
```

差異只剩一點：Android 能用的 overlay 受限於 `dtbo.img` 的內容。要在 Android 用一個新的 Yocto overlay，必須先加到 BoardConfig.mk 並重 build `dtbo.img`。

## 7. 結論與建議

1. **用格式本來就有的欄位。** `dt_table_entry.id` 和 `mkdtimg --id` 都是現成的，把它設成名稱的 crc32，就能讓「名稱 → entry」不依賴任何手動編號或順序。
2. **bootloader 讀外部資料一定要做範圍檢查。** 原本的 enum 位置、`dt_size`、`dt_offset` 都沒有檢查；即使 dtbo 有 AVB 保護，舊 image 搭新 U-Boot 一樣會出事。
3. **overlay 套用失敗要還原 base fdt。** `fdt_overlay_apply()` 失敗後 base tree 處於未定義狀態，跳過該 overlay 前要先還原，否則會帶著壞掉的 DT 開機。
4. **附加的 Android 屬性跟著 overlay 走。** 寫到 `/firmware/android`，不靠 `append_bootargs`，重燒 SD 卡後只要重設 overlay 清單即可。
5. **Android 和 Yocto 共用 U-Boot 時，env 介面要一致。** 同一塊板子只需要記一組指令。
6. **待辦：**
   - 舊專案（i.MX8MP）尚未在實板驗證新 U-Boot + 帶 id 的 dtbo（DSI 面板、RS422/RS485 overlay）。新做法讓它的 RS422/RS485 可以直接用完整 dtbo 名稱，不必再改 U-Boot，但同樣未實測。
   - 可以在 `dtbo.mk` 加一個檢查，確認同一個 `dtbo.img` 裡 overlay 的 id 沒有重複。

### 常見陷阱

| 陷阱 | 說明 |
|---|---|
| 只改 `.dtso` 不會重新打包 `dtbo.img` | `dtbo.mk` 沒有追蹤 `.dtbo` 的相依。要先刪掉 `dtbo*.img`、`vbmeta*.img` 再 build |
| 燒 SD 卡會清掉 U-Boot env | 燒錄後 env 回到預設（`boot_fdt_overlay=no`），要重新設 overlay |
| 只 merge U-Boot、沒 merge `dtbo.mk` | dtbo 沒有 id，完整名稱都找不到，overlay 全部 skip（不會當機） |
| 4K overlay 套在 1080p 螢幕上 | 可以正常顯示，但 density 會是 640，畫面看起來放大兩倍；回 1080p 前要關掉 overlay |
| LVDS 和 4K overlay 同時開 | 4K overlay 把 DPU clock 設為 297 MHz，可能影響 LVDS 時序；這次是分開測，沒有驗證兩者並用 |
| `dtbo.img` 的 md5 每次 build 都不同 | AVB hash footer 用隨機 salt，比對內容要用 `mkdtimg dump` 看 id |

## 附錄

### A. 常用指令

```bash
# Android 端（userdebug，root）設定 overlay：和 Yocto 相同的變數
adb root
adb shell fw_setenv boot_fdt_overlay yes
adb shell fw_setenv fdt_overlay_files "imx95-smarc-ov5640.dtbo imx95-smarc-lvds-g121xcel01.dtbo"
adb shell fw_printenv boot_fdt_overlay fdt_overlay_files dtoverlay
adb reboot

# 舊的 dtoverlay 仍可用（名稱同樣用 dtbo 檔名）
adb shell fw_setenv dtoverlay "imx95-smarc-ov5640 imx95-smarc-lvds-g121xcel01"

# 關掉 overlay
adb shell fw_setenv boot_fdt_overlay no
```

```text
# U-Boot console
=> setenv boot_fdt_overlay yes
=> setenv fdt_overlay_files "imx95-smarc-ov5640.dtbo imx95-smarc-lvds-g121xcel01.dtbo"
=> saveenv
=> boot

# 開機時 console 會看到
ANDROID: dtoverlay imx95-smarc-ov5640 applied
ANDROID: dtoverlay imx95-smarc-lvds-g121xcel01 applied
ANDROID: dtoverlay foo not found, skip
```

```bash
# 開機後確認
getprop ro.boot.camera.layout
getprop ro.boot.lcd_density
getprop ro.boot.displaymode
ls /proc/device-tree/firmware/android/
cat /proc/device-tree/soc/display-controller@4b400000/assigned-clock-rates | xxd   # 4K overlay 是否生效

# 重新打包 dtbo（改了 dtso 之後）
rm -f out/target/product/<product>/dtbo*.img out/target/product/<product>/vbmeta*.img
# 再照平常的方式 build

# 檢查 dtbo.img 的 entry 與 id
out/host/linux-x86/bin/mkdtimg dump out/target/product/<product>/dtbo-imx95.img

# 自己算一個名稱的 id（和 dtbo.mk 相同）
printf %s imx95-smarc-ov5640 | gzip -c | tail -c 8 | head -c 4 | od -An -tx4
```

libubootenv 的 `fw_env.config`（SD 卡開機）指向 `/dev/block/mmcblk1 0x700000 0x4000`；從 eMMC 開機時要改成 `mmcblk0`。

### B. 參考資料

- U-Boot：`drivers/fastboot/fb_fsl/fb_android_dtbo.c`、`fb_android_dtbo.h`（`android_apply_dtoverlay`、`find_dtbo`、`apply_dtbo`）
- U-Boot：`drivers/fastboot/fb_fsl/fb_fsl_boot.c`（`do_boota()` 呼叫點）
- U-Boot：`include/dt_table.h`（`dt_table_header`、`dt_table_entry`，來自 AOSP `system/libufdt`）
- U-Boot：`board/<vendor>/<board>/<board>.env`（`boot_fdt_overlay`、`fdt_overlay_files`、Yocto 的 `loadoverlay`）
- device/nxp：`common/build/dtbo.mk`、`imx9/<product>/BoardConfig.mk`（`TARGET_BOARD_DTBO_CONFIG`）
- TechNexion uboot-imx a54aad0「fb_fsl: Android dtoverlay extention」：https://github.com/technexion-android/uboot-imx/commit/a54aad02aad21aa7709a8e3b2b6c7f2568adcb3a
- Android DTB/DTBO partition 格式：https://source.android.com/docs/core/architecture/dto/partitions
