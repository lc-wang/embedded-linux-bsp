# FRDM-IMX93 M33：pixpaper-213 (SSD1680) E-Paper 移植報告

## 1. 目標

在 NXP FRDM-IMX93 上,讓 Cortex-M33 核心(透過 Linux **remoteproc** 啟動的 Zephyr firmware)
驅動一片 2.13" 單色電子紙 **pixpaper-213(控制器 SSD1680,250×122)**,經由 **LPSPI3** 傳輸。

參考來源:已驗證可動的 Linux user-space 程式
`test/user-space-examples/2.13/mono/spi/pixpaper-213-m-test-frdm-imx93.c`(spidev + libgpiod)。

---

## 2. 環境

| 項目 | 路徑 / 版本 |
|------|------|
| Zephyr | v4.4.0-2084-g6c7aadb0fc68（上游 <https://github.com/zephyrproject-rtos/zephyr>）|
| Zephyr SDK | zephyr-sdk-1.0.0-rc1 |
| Linux kernel | 6.12.34,vendor BSP kernel（NXP i.MX BSP 衍生,上游 <https://github.com/nxp-imx/linux-imx>）|
| Board target | `frdm_imx93/mimx9352/m33`(TCM:ITCM 0x0FFE0000 / DTCM 0x20000000) |
| 啟動方式 | Linux remoteproc 載入 ELF 啟動 M33 |

---

## 2.5 最終結論(RESOLVED）

> 以下是最後定論。§3 之後是完整排查歷程(含數個事後被推翻的中途假設),保留作紀錄。

**兩個獨立問題,兩個 fix:**

1. **remoteproc 啟動全系統卡死** → M33 用到的周邊時脈被 Linux `clk_disable_unused` 關掉,存取即 bus stall。
   Fix:`clk-imx93.c` 對 LPUART2 root/gate 標 `CLK_IS_CRITICAL`(其餘 LPSPI3/GPIO2/EDMA2 是後來探索 DMA 時加的,**最終方案不需要**,可還原)。

2. **e-paper 畫面殘缺(noise / thirds / half / 壓縮半高)** → **根因:i.MX93 LPSPI 在「單筆含 ≥2 個 word 的傳輸」會在固定的第 11 個 word 起失步掉資料。**
   - **實測 chunk 掃描(面板 ground truth,upstream 驅動)**:每筆 ≤**10** byte → **完整正確**;**11 起壞**;16 ≈ 2/3;整包 ≈ 半高壓縮。**「前 10 個 word 必定完好」是固定計數**。
   - **與時脈無關**(5MHz / 2MHz 皆同)、**與 CS 連續性無關**:清掉 `TCR[CONT]`(per-word PCS)、或維持連續(提早補料 watermark + 補料設 `CONTC`)**兩個方向都無效**。
   - **不是** FIFO underrun(連 16-byte「單次填滿 FIFO、零補料、零 underrun」都已壞;TX watermark 無效甚至更糟)、**不是** RX FIFO 溢位(每 IRQ 排空 RX 無效)、**不是** DMA 能解(eDMA 讀不到 M33 TCM)、**不是** 面板/幾何(chunk=1 用同樣 width/height/資料即完整)。
   - **與 full-duplex 症狀同源**:loopback `spi_transceive` 也是只回前 ~**10** bytes(`leading_match≈10`)再 timeout —— 同一個「前 10 word 後失步」指紋,指向 `spi_nxp_lpspi` 多 word 傳輸處理的固定 off-by-N。

**最終採用方案:標準 Zephyr `ssd16xx` 驅動 + mipi-dbi 資料分段傳輸。**
- 驅動層唯一改動:新增 opt-in 的 `CONFIG_MIPI_DBI_SPI_DATA_CHUNK_SIZE`(預設 0=單次傳輸,不影響其他顯示器),在 `drivers/mipi_dbi/mipi_dbi_spi.c` 的 4-wire 8-bit 路徑把 data phase 切成該大小的多筆 SPI(CS 段間 cycle)。本面板設 **8**(實測 ≤10 完整、11 起壞,取 8 留 margin;比逐 byte 快 8 倍)。
- 面板 sample 設定:ssd1680 `height=128`(source 寫滿 16 byte,否則少一條 strip)、**不要定義 `partial` profile**(走 full refresh,`0x22=0xF7`)。
- **不需要**:`0x21` source 選擇、LPSPI watermark、RAM 掃描 hack、eDMA、降頻 —— 全是被推翻的中途嘗試,已還原。

> 更深層的 bug:「LPSPI 連續多-byte 傳輸掉 byte」會影響此平台上**任何**大型 SPI 傳輸,值得另案修上游(治本後連分段都不需要)。raw-SPI 版(`pixpaper_213m`)仍保留作對照/備援。

**驗證結果(實機)**:`epaper_pixpaper_ssd16xx` 以**標準 ssd16xx 驅動 + 標準 display API** 跑確定性測試(全黑/全白/半分皆乾淨),並成功顯示 `test.png`(img0 打包成 MONO10/VTILED/MSB-first buffer,經 `display_write()`)。影像方向、極性、覆蓋全部正確。兩個 sample 都可用:
> - `pixpaper_213m` — raw-SPI 逐 byte(自帶序列,不依賴 driver)
> - `epaper_pixpaper_ssd16xx` — 標準驅動 + display API(可接 CFB/LVGL),靠 `CONFIG_MIPI_DBI_SPI_DATA_CHUNK_SIZE=1` 繞過 LPSPI 大傳輸 bug

---

## 2.6 交叉驗證與治本嘗試(Linux 對照 + driver-fix)

§2.5 把根因定位到「LPSPI CPU/PIO 模式單筆多-word 傳輸掉資料」。後續做了兩件事把它釘死,並嘗試治本。

### Linux 同硬體交叉驗證(決定性)
把同一個 LPSPI3 + 同一片面板交給 Linux 跑(M33 停掉、改 spidev DT,userspace 程式可調 chunk):

| 條件 | 結果 |
|------|------|
| Linux **DMA**(lpspi3 預設有 eDMA)@1MHz,單次 4000-byte | 完整 |
| Linux **PIO**(DT 移除 `dmas`/`dma-names`)@1MHz / **5MHz**,單次 4000-byte | 完整 |
| **Zephyr PIO @5MHz**(M33 被迫 PIO)單次 4000-byte | 半張 |

> 同一顆 LPSPI、同樣 PIO、同樣 5MHz、同樣單次大傳輸 —— **Linux 對、Zephyr 錯** → bug 在 **Zephyr `spi_nxp_lpspi` 的 PIO 實作**,不在 silicon。
> (M33 不能走 DMA:eDMA2 到不了 M33 TCM,所以 Zephyr 只能 PIO。)

`spi-fsl-lpspi`(Linux)vs `spi_nxp_lpspi`(Zephyr)PIO 差異:非零 watermark(`FCR=fifo/2`) vs `FCR=0`;Linux 每 IRQ 排空 RX(無 rx_buf 就丟) vs Zephyr TX-only 關 RDIE 不排空;Linux watermark(TDIE)+ frame-complete(FCIE) vs Zephyr 空了才補;Linux 跨 refill 管理 CONTC vs Zephyr 只設一次 CONT。

### Driver-fix 嘗試(8 輪,未竟全功)
把上述 Linux 做法移植進 `spi_nxp_lpspi`,核心改動(debug 計數器省略):

```c
/* (a) lpspi_handle_rx_irq():TX-only 也排空 RX,避免溢位 */
+	while ((rx_fsr = rx_fifo_cur_len(base)) > 0) {
+		if (spi_context_rx_on(ctx)) { lpspi_rx_buf_write_words(...); spi_context_update_rx(...); }
+		else { for (uint8_t i = 0; i < rx_fsr; i++) { (void)base->RDR; } }   /* 丟棄 */
+	}

/* (b) transceive():非零 watermark —— 此寫入無效,FCR 讀回 0 */
+	base->CR &= ~LPSPI_CR_MEN_MASK;
+	while (base->CR & LPSPI_CR_MEN_MASK) { }
+	base->FCR = LPSPI_FCR_TXWATER(config->tx_fifo_size / 2) |
+		    LPSPI_FCR_RXWATER(config->rx_fifo_size / 2);

/* (c) lpspi_handle_tx_irq():refill 補 CONTC 接續同一 CONT frame */
+	if (op_mode == SPI_OP_MODE_MASTER) { base->TCR |= LPSPI_TCR_CONT_MASK | LPSPI_TCR_CONTC_MASK; }
	lpspi_next_tx_fill(dev);

/* (d) lpspi_isr():TX-only 在 FIFO 全 clock 完才結束(不靠 RX 數) */
+	if (tx_only) {
+		if (tx_fifo_cur_len(base) != 0) {
+			if (!last_word_kicked) { last_word_kicked = true; base->TCR = base->TCR; }
+			return;
+		}
+		base->IER = 0; base->CR |= LPSPI_CR_RRF_MASK; lpspi_end_xfer(dev); return;
+	}
```

- **成果**:修好完成邏輯 —— 不再 hang(原本某些傳輸會 timeout)。
- **卡點**(面板仍半張,instrument 4000-word 傳輸):
  - 每 refill 邊界 TX FIFO 已空(`refill-empty=249/250`)→ **每 16-word 一次 underrun**。
  - 每 CONT frame 只活 ~11 word(`rx-drained=2750`、`fills=250`、`2750=250×11`)→ 對上 ≤10/壞於11 門檻。
  - **clock-independent**(2MHz/5MHz 數字逐位元相同)→ 非 ISR 速率問題。
  - `TEF`/`REF`(TX-underflow / RX-overflow 旗標)從未設定。
  - **`FCR` watermark 寫不進**(`transceive()` 寫後讀回 `0x0`,連先清 `MEN`+等它清都無效)→ 可能須在 `lpspi_configure()` 的 reset/disable 窗口內設。
- **結論**:根治需 LPSPI IP owner(RM + 示波器)釐清 `FCR` 設定時機與 CONT/CONTC 跨 refill 接續。對 M33 而言,**chunking(≤10 byte/筆,塞進 FIFO、零 refill)就是繞過的正解**。

實驗碼以 `#define LPSPI_FIX_RX_DRAIN`(預設 0 = upstream 行為)留在 `spi_nxp_lpspi.c`,供日後再實驗。

### Upstream
- issue **#111124** —— 含完整測試報告(上述交叉驗證 + chunk 掃描 + driver-fix 量化 + diff)。
- PR **#111126** —— 泛用、預設關閉的 `CONFIG_MIPI_DBI_SPI_DATA_CHUNK_SIZE`。

---

## 3. 排查歷程

### 問題一：remoteproc 啟動後「整個系統卡死」

**症狀**：用 remoteproc 啟動 M33,A55/Linux 整個凍結。

**診斷**：
- Zephyr 的 CCM 時脈驅動 `clock_control_mcux_ccm_rev2.c` 的 `on()` **不會開周邊 root clock**
  (只處理 ENET/SAI/FLEXSPI,LPUART/LPSPI 走到 no-op),它假設時脈在啟動前已配好。
- 但 M33 用到的 LPUART2 / LPSPI3 / GPIO2 在 Linux DT 是 `disabled`,於是 Linux 開機尾段的
  `clk_disable_unused()` 把這些「沒人用」的時脈關掉。
- M33 啟動後一存取這些周邊暫存器 → **時脈是關的 → AHB/AXI bus stall → 整顆 SoC 凍結**。
- `clk_ignore_unused` bootarg **無效**,因為這些時脈開機時就是關的(不是「開著但未使用」)。

**驗證**(啟 M33 前):
```
$ grep -E 'lpuart2|lpspi3|gpio2' /sys/kernel/debug/clk/clk_summary
lpuart2_root   0  0  0  24000000 ...   ← enable_count = 0(被關)
lpuart2        0  0  0  24000000 ...
```

**修改**：`drivers/clk/imx/clk-imx93.c`,對 M33 用到的周邊 root + gate 加上 `CLK_IS_CRITICAL`
(手法與 SoC 既有的 `cm33` / `m33_root` 一致):

| 行 | Clock | 用途 |
|----|-------|------|
| 87  | `lpuart2_root` | M33 console root |
| 208 | `lpuart2`(gate) | M33 console gate |
| 104 | `lpspi3_root` | e-paper SPI root |
| 225 | `lpspi3`(gate) | e-paper SPI gate |
| 190 | `gpio2`(gate) | DC/RST/BUSY 控制腳 |

改後 `clk_summary` 中這些 clock 的 `enable_count` ≥ 1,M33 存取不再 bus stall,卡死解決。

---

### 問題二：改用 Zephyr 原生 ssd1680 驅動 → 畫面雜訊

**做法**:用 Zephyr 內建 `ssd16xx` + `mipi-dbi-spi` 驅動,純靠 devicetree 接線。
- 發現 m33 SoC dtsi **缺 lpspi3 節點**,補上(`nxp,lpspi`,reg 0x42550000,IRQ 65,`IMX_CCM_LPSPI3_CLK`)。
- 加 mipi-dbi + ssd1680 子節點(width=250 height=122,tssv/border 參數比照同款 `gdey0213b74`)。
- 用 CFB(character framebuffer)印文字。

**症狀**:畫面只有雜訊。進一步用「均勻填色」測試:
- phase0 全 0x00、phase1 全 0xFF **都無法整片單色**,phase2「一半黑」只顯示約 1/3。

**診斷**(注:此處的初步歸因後來被修正,見 §3.1):
- console 顯示 `Panel ssd1680@0: 250x120`(高度 122 被驅動向下取整到 8 的倍數 → 120)、
  `load WS from OTP`、`Frame pushed` → **SPI / init / refresh 在 API 層全部成功**,BUSY 輪詢正常。
- 當下歸因為「RAM 幾何不符」並放棄通用驅動 —— 但這個結論其實**不正確**(見下節更正)。
- 正確原因:通用驅動把整個 framebuffer 用**單次 `mipi_dbi_command_write`** 送出
  (`drivers/display/ssd16xx.c:449` → `mipi_dbi_spi_write_helper` → 單次 `spi_write`),
  與 §3 問題三 看到的「單次大傳輸中間掉一段」**是同一個 LPSPI bug**。
  幾何那邊頂多少 2 行(122→120),不會整片雜訊;width/height 其實是可設的 property。

**(當下)結論**:先放棄通用驅動,改走忠實移植 user-space 序列(§3 問題三)。
正確的根因與「原生驅動其實可救」見 §3.1。

---

### 問題三：raw SPI 忠實移植 → 中間出現反相塊

**做法**:自訂 binding `open-ep,pixpaper-213m`,把面板當 raw SPI device(硬體 PCS0),
DC/RST/BUSY 用 GPIO,在 app 中完整重現 user-space 的初始化與 RAM 寫入序列:

```
reset → 0x12(SW reset)
0x01 = F9 00 00     driver output:0xF9+1 = 250 gates
0x11 = 01           data entry:X+ / Y-
0x44 = 00 0F        RAM X range 0..15(16 bytes)
0x45 = F9 00 00 00  RAM Y range 249..0
0x3C = 05  0x21 = 00 80  0x18 = 80  0x4E = 00  0x4F = F9 00
0x24 → 4000 bytes → 0x22 = F7 → 0x20(master activation)
```

**症狀**:均勻填色「大部分對了」(白→白、黑→黑),但**正中央一塊反相**,且在 phase 間反相
(殘留前一張)。

**診斷**:背景對、只有中間一段沒被覆寫 → 送 RAM 的 **4000-byte 資料,中間有一段沒寫進去**。
與 user-space 唯一差別:user-space 是**一次一個 byte** 送,我是**單次 4000-byte 大傳輸**。

**修改**:`epd_refresh()` 改成逐 byte 送(每 byte 一次 SPI transaction,與 user-space 完全一致):
```c
gpio_pin_set_dt(&dc_gpio, 1);              /* data */
for (size_t i = 0; i < sizeof(framebuf); i++)
        spi_tx(&framebuf[i], 1);
```

**結果**:phase 0/1/2 全部乾淨(全白 / 全黑 / 條紋皆正確)。移植管線成立。

---

### 3.1 根因更正:原生驅動失敗 = 同一個 LPSPI 大傳輸 bug(非幾何)

> **⚠️ 機制已再次修正(以 §2.5 為準)**:本節下方把主因歸為「FIFO underrun / CS refill 邊界」、
> 並提出「降頻」「eDMA 根治」—— 這些**後來都被面板實測推翻**。實測:連 16-byte 單次填滿
> FIFO(零補料、零 underrun)都已壞;分界是**固定第 11 個 word**、與時脈無關、與 CONT/CONTC/
> watermark/RX 排空皆無關(見 §2.5)。underrun/降頻/eDMA 段保留作排查紀錄,**結論不採用**。

事後追原生驅動的寫 RAM 路徑,確認 §3 問題二 的「幾何不符」歸因**不正確**:

- 原生 ssd16xx 寫 RAM:`ssd16xx.c:449` `SSD16XX_CMD_WRITE_RAM` → `mipi_dbi_command_write`
  → `mipi_dbi_spi_write_helper` → **單次 `spi_write` 送整個 framebuffer**。這跟 raw-SPI 的單次
  4000-byte 大傳輸是**同一條路**,所以原生驅動也踩到「中段掉資料」。
- **機制**(`drivers/spi/spi_nxp_lpspi/spi_nxp_lpspi.c`):CPU 模式 watermark=0(`FCR=0`,~L400),
  TX FIFO 僅 16 深,靠 TX 中斷補(`lpspi_next_tx_fill` L138-184)。5 MHz 下 16 bytes 約 25 µs
  就排空;若中斷延遲超過,FIFO underrun,且 refill 邊界沒重設 CONT/CS → SSD1680 失去 byte
  同步 → 掉中段。**逐 byte 之所以可解**:每次傳輸極小、CS 乾淨地 per-byte cycle,不會 underrun。
- 原生驅動 **有** `width`/`height` property(`display-controller.yaml`,必填),只是高度向下取整到 8
  的倍數(`ssd16xx.c:316`,122→120,底部少 2 行),這不是雜訊主因。

#### ERR050456 調查(結論:無關)
`CONFIG_SPI_NXP_LPSPI_ERR050456` 的 workaround(`spi_nxp_lpspi_common.c:322`:module reset +
RX/TX FIFO flush)發生在**每次傳輸「之前」**,管不到單一傳輸「中途」掉資料,且僅 S32K3 自動選用。
**無法解決本問題**。

#### 原生驅動重試:用 property 降頻(sibling sample)
新增獨立範例 `samples/boards/nxp/frdm_imx93/epaper_pixpaper_ssd16xx/`(原 raw-SPI 版保留不動),
回到原生 `ssd16xx` + `mipi-dbi-spi`,關鍵 property 調整:

| property | 值 | 理由 |
|------|------|------|
| `mipi-max-frequency` | **1 MHz**(起始) | 降頻 → FIFO 排空變慢 → 補 FIFO 的 ISR 來得及 → 不 underrun。這是唯一能從 DT 影響大傳輸的槓桿(watermark/CPU 模式非 property) |
| `width` / `height` | 250 / 122 | 幾何(實際可用高度 120) |
| `tssv` / border | 0x80 / 0x05 / 0x3c | 比照同款 gdey0213b74 |

`src/main.c` 用 display API 做確定性測試(全 0x00 / 全 0xFF / 半黑半白)。判讀:
- phase0/1 **乾淨全黑/全白** → 降頻修好了原生驅動的大傳輸;可逐步把頻率往 5 MHz 調高找上限,
  再換成 CFB 文字或影像(程式碼比 raw-SPI 乾淨)。
- 仍掉中段 → 降頻不足以解(問題不只時脈),維持 raw-SPI 為最終方案。

#### 降頻實測進度(behaviour 非單調)
| `mipi-max-frequency` | 結果 |
|------|------|
| 5 MHz | init 完成、能刷新,但大傳輸掉中段(雜訊) |
| 1 MHz | **init 讀 OTP 的 SPI 傳輸卡住**,~200ms 逾時(`spi_lpspi: Timeout waiting for transfer complete`)→ display probe 失敗。太低反而觸發完成中斷不發生 |
| 3 MHz | init 完成;畫面上 1/3 乾淨、中 1/3 灰(位元錯位)、下 1/3 未寫入(白)。= underrun 隨傳輸惡化,比 5 MHz 改善但未根治 |
| 2 MHz | 未進一步測試 —— 趨勢顯示降頻只降低 underrun 機率、無法消除,改走 eDMA 根治(下節) |

> 結論:降頻不是乾淨解(太高掉資料、太低卡 init、中間僅部分改善)。根因是 CPU 模式中斷延遲,
> 唯一根治是讓 FIFO 不靠 CPU 餵 → **eDMA**。

#### 3.2 根治:LPSPI3 改走 eDMA2(原生驅動可乾淨運作)

把 LPSPI3 接到 **eDMA2**,DMA 持續餵 TX FIFO、無 CPU 中斷延遲 → 大傳輸不再 underrun,
原生 ssd16xx 驅動的單次 framebuffer 傳輸即可完整,時脈可維持 5 MHz。

硬體對應(i.MX93,取自 Linux dts):LPSPI3 在 **eDMA2**(wakeupmix,0x42000000,eDMA v4),
**TX request source = 12、RX = 13**;Zephyr 用 eDMA channel 0(tx)/1(rx),兩者共用 M33 NVIC IRQ 128。

變更:
| 檔案 | 變更 |
|------|------|
| `dts/arm/nxp/imx/nxp_imx93_m33.dtsi` | 新增 `edma2` 節點(`nxp,mcux-edma` `nxp,version=4`,IRQ 128/129/130 對應 ch0-5,預設 disabled) |
| `epaper_pixpaper_ssd16xx/boards/*.overlay` | `&edma2 status=okay`;`&lpspi3` 加 `dmas=<&edma2 0 12>,<&edma2 1 13>` + `dma-names="tx","rx"`;頻率回 5 MHz |
| `kernel_imx .../clk-imx93.c` | **再加 `IMX93_CLK_EDMA2_GATE` 為 `CLK_IS_CRITICAL`**(共 6 處;否則 M33 碰 eDMA2 一樣 bus stall) |

DMA 啟用是 DT 驅動、自動的:lpspi3 一有 `dmas` 屬性,`CONFIG_SPI_NXP_LPSPI_DMA` 自動開;
edma2 節點的 `nxp,version=4` 自動帶起 `CONFIG_DMA_MCUX_EDMA_V4`。build .config 確認:
`CONFIG_SPI_NXP_LPSPI_DMA=y` / `CONFIG_DMA_MCUX_EDMA_V4=y` / `CONFIG_SPI_NXP_LPSPI_CPU` 未設。

> 注意(資源歸屬):eDMA2 是 wakeupmix 共用 DMA 控制器,Linux 端其他周邊也可能用它。M33 佔用
> channel 0/1,Linux 須避開這兩個 channel(eDMA 各 channel 暫存器獨立,但 MP 控制區共用)。
> 實測前先確認 Linux 沒把 edma2 ch0/1 指派給自己。

> 另一個已知安全的折衷:**逐 column 送(16 bytes)= 剛好 FIFO 深度**,單次填滿即清空、無 refill 邊界,
> 故不會 underrun;比逐 byte 快很多,適用於 raw-SPI 版加速。

---

### 最後一步:顯示真實影像 `png_HEX.h`

- `png2bit.py` 產生的格式:每列(image 高度 row)`(width+31)/32` 個 uint32,bit MSB-first 對 column,
  **bit 1 = 白、0 = 黑**。本圖 test.png = **211×103** → 每列 7 個 uint32,共 721 個。
- 映射(與 user-space 一致):image 寬(211)→ panel **gate 軸**,image 高(103)→ **source 軸**,
  不足處補白:
  ```c
  word = img0[row * 7 + (col / 32)];
  white = (word >> (31 - (col % 32))) & 1;   /* col=gate, row=src */
  ```
- 流程:`epd_init()` → 清成全白(去殘影)→ 顯示影像 → idle(電子紙斷電保留)。

**結果**:畫面正常顯示 test.png。✅

---

## 4. 變更檔案總覽

### Linux kernel(`imx93/kernel_imx`)
| 檔案 | 變更 |
|------|------|
| `drivers/clk/imx/clk-imx93.c` | LPUART2 / LPSPI3 / GPIO2 共 5 處加 `CLK_IS_CRITICAL` |

### Zephyr(`zephyr/zephyr`)
| 檔案 | 變更 |
|------|------|
| `dts/arm/nxp/imx/nxp_imx93_m33.dtsi` | 新增 `lpspi3` 節點(原本 m33 dtsi 缺) |
| `dts/bindings/display/open-ep,pixpaper-213m.yaml` | 新增 raw-SPI 面板 binding(spi-device + dc/reset/busy gpios) |
| `samples/boards/nxp/frdm_imx93/pixpaper_213m/` | 新範例（raw-SPI,最終方案）:`CMakeLists.txt` / `prj.conf` / `src/main.c` / `src/png_HEX.h` / `boards/frdm_imx93_mimx9352_m33.overlay` |
| `samples/boards/nxp/frdm_imx93/epaper_pixpaper_ssd16xx/` | 原生驅動重試範例（§3.1,降頻實驗):`mipi-dbi-spi` + `solomon,ssd1680`,`mipi-max-frequency=1MHz` |

> 註:GPIO2 的 DC/RST/BUSY pad mux **不需額外設定** —— SoC dtsi 已有完整 gpio2 pinmux 陣列,
> rgpio 驅動在 `gpio_pin_configure` 時會自動套用。LPSPI3 pad 則沿用 board 既有的 `spi3_default`。

### 硬體接線
| 訊號 | 腳位 | 說明 |
|------|------|------|
| SPI | LPSPI3,硬體 PCS0,5 MHz,Mode 0 | SCK=IO11 / SIN=IO09 / SOUT=IO10 / PCS0=IO08 |
| DC | GPIO2_IO00 | low = command,high = data |
| RST | GPIO2_IO05 | low = reset |
| BUSY | GPIO2_IO26 | high = busy |

---

## 4.1 Linux 端資源歸屬(避免雙核衝突)

M33 現在獨佔的硬體,Linux 端**不能同時驅動**,否則兩核搶同一個控制器會導致資料毀損。
經查 frdm 這份 DT **目前不需修改**,所有 M33 用到的資源在 Linux 端都是空的:

| 資源 | Linux frdm DT 狀態 | 結論 |
|------|------|------|
| `lpspi3`（spi@42550000） | base dtsi `disabled`,frdm/evk 都未打開 | ✅ 不衝突 |
| `lpuart2` | 未被任何 board dts 引用(維持 disabled) | ✅ 不衝突 |
| GPIO_IO00 / IO05（DC/RST） | Linux 完全未引用 | ✅ 不衝突 |
| GPIO_IO08~11（LPSPI3 pad） | Linux 完全未引用 | ✅ 不衝突 |
| GPIO_IO26（BUSY） | 僅出現在 `pinctrl_sai3` / `pinctrl_sai3_sleep`,而 frdm.dts 已 `&sai3 { status="disabled"; }` → pinctrl 不會被套用 | ✅ 不衝突 |

**重要注意事項**:
- 跑 user-space 參考程式時用的是 `/dev/spidev0.0` + gpiochip,那需要另一份「**有打開
  lpspi3 + spidev、並把 IO00/05/26 設為 gpio**」的 DT。**跑 M33 版時切勿套用那份 DT** ——
  否則 Linux 的 spidev 會 bind 到 lpspi3、Linux 也會持有那些 GPIO,直接與 M33 相撞。
  維持目前這份 frdm DT(只加了 5 處 `CLK_IS_CRITICAL`)即可。
- `CLK_IS_CRITICAL` 只是讓時脈保持開啟供 M33 使用,與「Linux 是否驅動該周邊」是兩回事 ——
  時脈開著但 Linux 未 bind 驅動,不會衝突,這正是預期狀態。
- 若日後要在 Linux 端啟用其他周邊,務必避開 M33 已佔用的 LPSPI3 / LPUART2 / GPIO2 IO00/05/26/08-11。

---

## 5. 建置與執行

```bash
cd <zephyr-workspace>/zephyr
source ../.venv/bin/activate
export ZEPHYR_SDK_INSTALL_DIR=$PWD/../zephyr-sdk-1.0.0-rc1
west build -p always -b frdm_imx93/mimx9352/m33 \
    samples/boards/nxp/frdm_imx93/pixpaper_213m
# 產物 build/zephyr/zephyr.elf → remoteproc 載入啟動
```
Footprint:FLASH ~32%(128KB)/ RAM ~14%(124KB),全程在 TCM。

預期 console:
```
*** Booting Zephyr OS ... ***
[inf] pixpaper_213m: Initializing pixpaper-213m over LPSPI3...
[inf] pixpaper_213m: Init done
[inf] pixpaper_213m: Cleared to white
[inf] pixpaper_213m: Displayed image (211x103)
```

---

## 5.1 sample 目錄結構與各檔案角色

```
samples/boards/nxp/frdm_imx93/pixpaper_213m/
├── CMakeLists.txt                              # Zephyr app 進入點(find_package + target_sources)
├── prj.conf                                    # Kconfig:只開 SPI / SPI_NXP_LPSPI / GPIO / LOG
├── boards/
│   └── frdm_imx93_mimx9352_m33.overlay         # 接線:啟用 lpspi3 + epd 節點 + DC/RST/BUSY gpios
├── src/
│   ├── main.c                                  # 驅動主程式(init / refresh / 影像映射)
│   └── png_HEX.h                               # 由 png2bit.py 產生的影像資料(img0[])
└── REPORT.md                                   # 本報告
```

各檔案職責:

| 檔案 | 作用 | 關鍵內容 |
|------|------|------|
| `CMakeLists.txt` | 宣告這是一個 Zephyr 應用 | `find_package(Zephyr)` + `target_sources(app PRIVATE src/main.c)` |
| `prj.conf` | 啟用最小驅動集 | `CONFIG_SPI=y` `CONFIG_SPI_NXP_LPSPI=y` `CONFIG_GPIO=y` `CONFIG_LOG=y`(刻意不開 DISPLAY/SSD16XX/MIPI_DBI/CFB) |
| `boards/*.overlay` | board 專屬 devicetree 疊加 | 啟用 `&lpspi3`、定義 `epd@0`(compatible `open-ep,pixpaper-213m` + DC/RST/BUSY gpios) |
| `src/main.c` | 應用邏輯 | pixpaper-213m init/refresh 序列、framebuffer 映射 |
| `src/png_HEX.h` | 影像資料 | `const uint32_t img0[]`(211×103) |

> 此外有兩個檔案在 sample 之外、屬於整體整合的一部分:
> - `zephyr/dts/arm/nxp/imx/nxp_imx93_m33.dtsi` — 新增 `lpspi3` 節點(SoC 層,sample overlay 才能 `&lpspi3`)
> - `zephyr/dts/bindings/display/open-ep,pixpaper-213m.yaml` — `open-ep,pixpaper-213m` 的 binding(overlay 的 compatible 才認得)

### 從零建構的步驟回顧
1. 在 SoC dtsi 補 `lpspi3` 節點(原本缺)。
2. 寫 `open-ep,pixpaper-213m` binding(`include: spi-device.yaml` + dc/reset/busy gpios)。
3. 建 sample 四件套:`CMakeLists.txt` / `prj.conf` / `boards/<board>.overlay` / `src/main.c`。
4. overlay 啟用 lpspi3(掛 board 既有的 `spi3_default` pinctrl)並定義 `epd@0`。
5. `main.c` 用 `SPI_DT_SPEC_GET` / `GPIO_DT_SPEC_GET` 取得裝置,重現面板序列。
6. 用 `png2bit.py` 把圖轉成 `png_HEX.h` 放進 `src/`。

---

## 5.2 重要程式碼

**(a) overlay — 把面板掛成 raw SPI 裝置**(`boards/frdm_imx93_mimx9352_m33.overlay`)
```dts
&lpspi3 {
    status = "okay";
    pinctrl-0 = <&spi3_default>;      /* board 既有的 LPSPI3 pinmux */
    pinctrl-names = "default";
    #address-cells = <1>;
    #size-cells = <0>;

    epd: epd@0 {
        compatible = "open-ep,pixpaper-213m";
        reg = <0>;                    /* 硬體 PCS0 */
        spi-max-frequency = <5000000>;
        dc-gpios   = <&gpio2 0  GPIO_ACTIVE_HIGH>;   /* low = command */
        reset-gpios = <&gpio2 5  GPIO_ACTIVE_HIGH>;  /* low = reset   */
        busy-gpios  = <&gpio2 26 GPIO_ACTIVE_HIGH>;  /* high = busy   */
    };
};
```

**(b) 裝置取得**(`main.c`)
```c
#define EPD_NODE DT_NODELABEL(epd)
static const struct spi_dt_spec epd_spi =
        SPI_DT_SPEC_GET(EPD_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const struct gpio_dt_spec dc_gpio  = GPIO_DT_SPEC_GET(EPD_NODE, dc_gpios);
static const struct gpio_dt_spec rst_gpio = GPIO_DT_SPEC_GET(EPD_NODE, reset_gpios);
static const struct gpio_dt_spec busy_gpio= GPIO_DT_SPEC_GET(EPD_NODE, busy_gpios);
```

**(c) 命令 / 資料 / BUSY 三個基本動作** —— DC 線決定 cmd/data,BUSY 高電位表示忙碌:
```c
static void epd_cmd(uint8_t c)  { gpio_pin_set_dt(&dc_gpio,0); k_busy_wait(1); spi_tx(&c,1); }
static void epd_data(uint8_t d) { gpio_pin_set_dt(&dc_gpio,1); k_busy_wait(1); spi_tx(&d,1); }
static void epd_wait_idle(void) {            /* 含 ~10s timeout,避免 M33 卡死 */
        k_msleep(2);
        for (int i=0;i<1000;i++){ if(gpio_pin_get_dt(&busy_gpio)==0) return; k_msleep(10);} }
```

**(d) 初始化序列**(完全比照 user-space;`epd_init()`)
```c
epd_hw_reset(); k_msleep(1000); epd_wait_idle();
epd_cmd(0x12); epd_wait_idle();                 /* SW reset            */
epd_cmd(0x01); epd_data(0xF9);epd_data(0x00);epd_data(0x00); /* 250 gates */
epd_cmd(0x11); epd_data(0x01);                  /* data entry X+ / Y-  */
epd_cmd(0x44); epd_data(0x00);epd_data(0x0F);   /* RAM X 0..15 (16B)   */
epd_cmd(0x45); epd_data(0xF9);epd_data(0x00);epd_data(0x00);epd_data(0x00); /* RAM Y 249..0 */
epd_cmd(0x3C); epd_data(0x05);  epd_cmd(0x21); epd_data(0x00);epd_data(0x80);
epd_cmd(0x18); epd_data(0x80);  epd_cmd(0x4E); epd_data(0x00);
epd_cmd(0x4F); epd_data(0xF9);epd_data(0x00);   epd_wait_idle();
```

**(e) 刷新 —— 關鍵的「逐 byte 送」**(`epd_refresh()`):
```c
epd_cmd(0x24); k_msleep(10);                    /* write RAM (B/W) */
gpio_pin_set_dt(&dc_gpio, 1);                   /* data */
for (size_t i = 0; i < sizeof(framebuf); i++)
        spi_tx(&framebuf[i], 1);                /* 一次一個 byte,單次大傳輸會掉中段 */
epd_cmd(0x22); k_msleep(10); epd_data(0xF7);    /* update control 2 */
epd_cmd(0x20); epd_wait_idle();                 /* master activation */
```

**(f) 影像映射**(`img_white()`/`fb_set()`)—— image 寬→gate 軸、高→source 軸,`bit 1 = 白`:
```c
/* framebuf 佈局:gate-major,每 gate 16 bytes(128 source bits) */
static void fb_set(int gate, int src, bool white) {
        uint8_t *b = &framebuf[gate*16 + src/8], m = 1u << (7-(src%8));
        if (white) *b |= m; else *b &= ~m;
}
static bool img_white(int gate, int src) {      /* col=gate, row=src */
        if (gate >= 211 || src >= 103) return true;             /* 補白 */
        return (img0[src*7 + gate/32] >> (31 - gate%32)) & 1u;  /* 7 = (211+31)/32 */
}
```

---

## 5.3 部署到 A55(Linux)與 remoteproc 啟動

M33 firmware(`zephyr.elf`)是放在 **A55 Linux 的檔案系統**裡,由 remoteproc 載入到 M33 執行。

### 1) 把 firmware 複製到 Linux 的 firmware 搜尋路徑
remoteproc 會從 `/lib/firmware/`(亦含 `/lib/firmware/updates` 等)尋找。把 build 產物複製過去:
```bash
# 在開發機 build 後
scp build/zephyr/zephyr.elf root@<board-ip>:/lib/firmware/
# 或板子上直接放:cp .../zephyr.elf /lib/firmware/
```

### 2) 指定 firmware 名稱並啟動(在板子上)
imx_rproc 預設 firmware 名稱是 `NULL`,**必須先用 sysfs 指定**:
```bash
# 確認 remoteproc 實例(通常 remoteproc0 即 cm33)
cat /sys/class/remoteproc/remoteproc0/name        # 應為 imx-rproc

# 指定 firmware 檔名(相對於 /lib/firmware/)
echo zephyr.elf > /sys/class/remoteproc/remoteproc0/firmware

# 啟動 M33
echo start > /sys/class/remoteproc/remoteproc0/state
```

### 3) 停止 / 重新載入
```bash
echo stop  > /sys/class/remoteproc/remoteproc0/state   # 停止
# 換新 firmware 後重複 firmware + start 即可
echo start > /sys/class/remoteproc/remoteproc0/state
```

### 4) 觀察 M33 console
M33 的 log 在 **LPUART2**(115200 8N1)。接上對應的 UART 即可看到:
```
*** Booting Zephyr OS ... ***
[inf] pixpaper_213m: Init done
[inf] pixpaper_213m: Displayed test.png (211x103)
```

> 前置條件提醒:
> - kernel 必須是含 5 處 `CLK_IS_CRITICAL`(§3 問題一)的版本,否則 `start` 後系統會卡死。
> - 不可套用 user-space spidev 測試用的那份 DT(§4.1),否則 LPSPI3/GPIO 會與 M33 相撞。
> - firmware 連結在 TCM,`m33_reserved@0xa5000000` 等保留區由 DT 既有設定涵蓋,無需另外處理。

---

## 6. 總結與教訓

1. **i.MX93 M-core + remoteproc 的時脈陷阱**:Zephyr 不會替 M33 開周邊 root clock,而 Linux 會把
   「未使用」的時脈關掉。任何 M33 要用的周邊,其時脈都必須在 Linux 端保持開啟
   (本案用 `CLK_IS_CRITICAL`),否則 M33 一存取就會 **bus stall 把整顆 SoC 凍死**。
   這是最隱晦、症狀最嚴重的一關。

2. **大型 SPI 傳輸的隱性問題(真正的主因)**:在此平台,LPSPI CPU 模式下「單筆含 ≥2 個 word 的
   傳輸」會在**固定的第 11 個 word 起失步掉資料**(前 10 word 必定完好)。chunk 掃描:≤10 完整、
   11 起壞、整包半高。**與時脈無關**、**非 FIFO underrun**(連 16-byte 單次填滿都壞)、**非 CS 連續性**
   (清 CONT 或補 CONTC 皆無效)、**非 RX 溢位**、**非幾何**(chunk=1 同資料即完整)。這也是原生
   `ssd16xx` 失敗主因(它是單次大傳輸)。與 full-duplex「只回前 ~10 byte 再 timeout」**同源**。
   解法:把 data phase 切成 **≤10-byte**(本案用 8 留 margin)。
   > 註:早期曾歸因 underrun 並嘗試降頻 / watermark / eDMA「根治」,**均被實測推翻**(§3.1 橫幅)。

3. **先別急著怪「驅動不相容」**:我一度把原生驅動雜訊歸因於 RAM 幾何,事後才發現是底層 SPI 傳輸。
   診斷關鍵是「**均勻填色**」這個與方向無關的測試,能快速把「資料對映/幾何」與「init/SPI 傳輸」
   兩類問題切開。`width`/`height` 其實是可設的 property(高度向下取整到 8 的倍數)。

4. **除錯方法論**:由症狀逐層往下切——
   console log(確認 init/SPI 是否成功)→ 均勻填色(切開資料 vs 硬體)→
   逐 byte(切開傳輸大小)→ 對照已知良品(user-space)。每一步都先用最小、確定性的測試驗證假設。

### 後續可優化
- **加速**:逐 byte(~1s/張)可改逐 column(16 bytes)分段傳輸,兼顧速度與避開大傳輸問題。
- **內容**:目前直接吃 `png_HEX.h`;可加繪文字/點陣圖 API,或調整影像擺放(置中/鏡像/翻轉,
  改 `img_white()` 的 `col/row` 即可)。
