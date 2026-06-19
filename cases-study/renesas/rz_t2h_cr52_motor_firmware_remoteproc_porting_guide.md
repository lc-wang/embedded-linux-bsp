
# RZ/T2H CR52 Motor Firmware 改造為 Linux Remoteproc 版本操作手冊

**平台**：Renesas RZ/T2H + Linux (A55) + CR52 Motor Firmware  
**工具**：e² studio（Windows）、Arm GCC、Renesas FSP  
**目標**：

-   保留原廠 Motor Control 全功能（PWM + DSM + Encoder + UART 命令介面）
-   讓 CR52 韌體改成 **由 Linux remoteproc 啟動**   
-   不破壞 Linux（避免 MMC -84 / EXT4 error / 卡死）
----------

## 0. 前置條件與專案說明

-   手上的 e² studio 專案名稱假設為：  
    `RZT2H_INVBLV_SPM_ENCD_FOC_E2S_V100`
-   這是原廠 FSP 產生 + motor control firmware 的專案。
-   Linux 端已經有：
    -   `remoteproc` 驅動
    -   `cr52_0` 對應的 `/sys/class/remoteproc/remoteproc0` 介面
-   本手冊所有修改，**請在複製後的新專案上做**，不要破壞原始版本。
    
----------

## 1. 用 e² studio 建立「Remoteproc 版」專案

1.  在 e² studio 專案視窗中：
    
    -   右鍵原專案 `RZT2H_INVBLV_SPM_ENCD_FOC_E2S_V100`
    -   選 **Copy** → 再點空白處 → **Paste**
2.  輸入新名稱（建議）：
    -   `RZT2H_INVBLV_SPM_ENCD_FOC_E2S_V100_remoteproc` 
3.  確認新專案可以直接 Build 成功（此時還不是 remoteproc-safe，只是確認環境正常）。

----------

## 2. 修改 Linker Script：改成 CR52 SRAM + remoteproc 入口

### 2.1 新增檔案

在新專案底下建立：
-   路徑建議：`script/rzt2h_cr52_remoteproc.ld`

內容範例
```sh
ENTRY(_start)

/* Firmware in CR52 SRAM
 * DTS:
 *   cr52_sram @ 0x10000000 - 0x101FFFFF (2MB)
 *
 * remoteproc start_address:
 *   0x10061000
 */

MEMORY
{
    FW (rxw) : ORIGIN = 0x10061000, LENGTH = 0x00019F000
}

/* 提供堆疊符號給組語 */
_estack = ORIGIN(FW) + LENGTH(FW);

SECTIONS
{
    .vectors :
    {
        KEEP(*(.vectors))
    } > FW

    .text :
    {
        *(.text*)
        *(.gnu.linkonce.t.*)
        *(.ARM.extab*)
        *(.ARM.exidx*)
        *(.rodata*)
        *(.gnu.linkonce.r.*)
    } > FW

    .init_array :
    {
        KEEP(*(.init_array*))
    } > FW

    .fini_array :
    {
        KEEP(*(.fini_array*))
    } > FW

    .data :
    {
        _sdata = .;
        *(.data*)
        *(.gnu.linkonce.d.*)
        _edata = .;
    } > FW

    .bss (NOLOAD) :
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        __bss_end__ = .;
    } > FW

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _stack_bottom = .;
        . = . + 0x2000;  /* 8KB stack */
        _stack_top = .;
    } > FW

    .resource_table :
    {
        KEEP(*(.resource_table*))
    } > FW
}` 
```
> 註：remoteproc 可以接受「沒有 resource table」的 firmware。

### 2.2 在 e² studio 指定 Linker Script

1.  專案右鍵 → **Properties** 
2.  左側選單：  
    `C/C++ Build` → `Settings` → `Tool Settings`
    
3.  在 **GNU Arm Cross C Linker** → `General`（或 Script 選項）中：
    
    -   找到「Script file」或 `-T` 相關設定      
    -   指定為：`script/rzt2h_cr52_remoteproc.ld`      
4.  套用（Apply），關閉視窗。
    
----------

## 3. 移除「自帶 reset / clock 設定」的啟動碼

### 3.1 停用 startup / crt 檔案

remoteproc 會：

-   設定 PC（起始位址）
-   設定 SP（stack）
-   啟動 clock / reset CR52 
→ firmware 不可以再自己做一次 reset 流程。

在專案中找到類似檔案：

-   `startup.c` 或 `startup_core.c`
-   任何 `*_start.c` / `vector_table.c` 若是自行重設 stack / reset handler 的，也需確認。
    

對這些檔案：

1.  右鍵檔案 → **Properties**
2.  `C/C++ Build` → **Settings** → 勾選 **Exclude from build**
3.  對 Debug / Release configuration 都做一次確認。
    

> 如果有 `.s` / `.asm` 的啟動碼檔案，也同樣排除。



## 4. 修改 hal_entry.c：接管 main 流程給 remoteproc

### 4.1 原始結構（簡化）

原本 hal_entry 大概長這樣：
```c
void  hal_entry(void)
{
    R_RWP_NS->PRCRN = 0x0000A50F;
    R_RWP_S->PRCRS = 0x0000A50F;
    R_SYSC_NS->MSTPCRA_b.MSTPCRA00 &= 0xFFFFFFFE;
    R_SYSC_NS->MSTPCRC_b.MSTPCRC05 = 0;

    R_Systeminit();
    m_startup(); // disable / enable interrupt 一大段  while (1)
    {
        m_foreground();
    }
}
```
→ **這段一定會讓 Linux 掛掉**，因為改了 PRCR / MSTP（由 Linux 控制）。

### 4.2 Remoteproc-safe 寫法

請改成：
```c
void hal_entry(void)
{
    /* Remoteproc 模式：不再操作 PRCR/MSTP/clock，避免干擾 Linux */

    /* 初始化 motor 系統（remoteproc-safe 版本） */
    R_Systeminit();               // 第 5 章會說明
    m_startup_remoteproc_safe();  // 第 6 章會說明

    /* 主控制迴圈：保留原本 motor 控制邏輯 */
    while (1)
    {
        m_foreground();
    }
}
```
> 重點：
> 
> -   **完全移除** `R_RWP_NS->PRCRN` / `R_RWP_S->PRCRS` / `MSTPCRA` / `MSTPCRC` 那些行
>     
> -   不在這裡處理中斷控制，只保留邏輯主迴圈。
>     

----------

## 5. 修改 R_Systeminit()：保留 Motor、避免 Linux Crash

### 5.1 原始版本會做的事

原版 `R_Systeminit()` 一般會包含：
-   `R_IOPORT_Open()` → 改 pinmux（會影響 MMC / UART / OSPI）
-   `R_BSP_RegisterProtectDisable()` → 解除保護，設定 PRCR、MRCTLA
-   `R_SYSC_NS->MSTPCR*` → 開關模組 clock
-   `R_XSPI_OSPI_Open()` / `ospi_set_DTR_OPI_Mode_enable()`
-   `EI()` / `DI()` 等
    

這些對於「Linux 早就初始化好的 SoC」來說，**都是高風險操作**。

### 5.2 Remoteproc-safe 版

將 `R_Systeminit()` 改為下列內容：
```c
void R_Systeminit(void)
{
    /* 設定每顆馬達的編碼器型態 */
    for (uint32_t idx = 0; idx < MOTOR_NUM; idx++)
    {
        g_st_m[idx].encoder_type = ETYPE_APE_FACODER;
    }

    /* 下面這些模組不會影響 Linux，可正常初始化 */
    R_ELC_Open(&g_elc_ctrl, &g_elc_cfg);
    R_POEG_Open(&g_poeg0_ctrl, &g_poeg0_cfg);

    /* GPT 三相馬達 PWM 控制器初始化 */
    R_GPT_THREE_PHASE_Open(&g_three_phase0_ctrl, &g_three_phase0_cfg);
    R_GPT_THREE_PHASE_Open(&g_three_phase1_ctrl, &g_three_phase1_cfg);
    R_GPT_THREE_PHASE_Open(&g_three_phase2_ctrl, &g_three_phase2_cfg);
    R_GPT_THREE_PHASE_Open(&g_three_phase3_ctrl, &g_three_phase3_cfg);
    R_GPT_THREE_PHASE_Open(&g_three_phase4_ctrl, &g_three_phase4_cfg);
    R_GPT_THREE_PHASE_Open(&g_three_phase5_ctrl, &g_three_phase5_cfg);
    R_GPT_THREE_PHASE_Open(&g_three_phase6_ctrl, &g_three_phase6_cfg);
    R_GPT_THREE_PHASE_Open(&g_three_phase7_ctrl, &g_three_phase7_cfg);
    R_GPT_THREE_PHASE_Open(&g_three_phase8_ctrl, &g_three_phase8_cfg);

    /* 關掉所有 GPT 的輸出（避免上電瞬間亂脈衝） */
    R_GPT00_0->GTIOR_b.OAE = 0;  R_GPT00_0->GTIOR_b.OBE = 0;
    R_GPT00_1->GTIOR_b.OAE = 0;  R_GPT00_1->GTIOR_b.OBE = 0;
    R_GPT00_2->GTIOR_b.OAE = 0;  R_GPT00_2->GTIOR_b.OBE = 0;
    R_GPT01_0->GTIOR_b.OAE = 0;  R_GPT01_0->GTIOR_b.OBE = 0;
    R_GPT01_1->GTIOR_b.OAE = 0;  R_GPT01_1->GTIOR_b.OBE = 0;
    R_GPT01_2->GTIOR_b.OAE = 0;  R_GPT01_2->GTIOR_b.OBE = 0;
    R_GPT02_0->GTIOR_b.OAE = 0;  R_GPT02_0->GTIOR_b.OBE = 0;
    R_GPT02_1->GTIOR_b.OAE = 0;  R_GPT02_1->GTIOR_b.OBE = 0;
    R_GPT02_2->GTIOR_b.OAE = 0;  R_GPT02_2->GTIOR_b.OBE = 0;
    R_GPT03_0->GTIOR_b.OAE = 0;  R_GPT03_0->GTIOR_b.OBE = 0;
    R_GPT03_1->GTIOR_b.OAE = 0;  R_GPT03_1->GTIOR_b.OBE = 0;
    R_GPT03_2->GTIOR_b.OAE = 0;  R_GPT03_2->GTIOR_b.OBE = 0;
    R_GPT04_0->GTIOR_b.OAE = 0;  R_GPT04_0->GTIOR_b.OBE = 0;
    R_GPT04_1->GTIOR_b.OAE = 0;  R_GPT04_1->GTIOR_b.OBE = 0;
    R_GPT04_2->GTIOR_b.OAE = 0;  R_GPT04_2->GTIOR_b.OBE = 0;
    R_GPT05_0->GTIOR_b.OAE = 0;  R_GPT05_0->GTIOR_b.OBE = 0;
    R_GPT05_1->GTIOR_b.OAE = 0;  R_GPT05_1->GTIOR_b.OBE = 0;
    R_GPT05_2->GTIOR_b.OAE = 0;  R_GPT05_2->GTIOR_b.OBE = 0;
    R_GPT06_0->GTIOR_b.OAE = 0;  R_GPT06_0->GTIOR_b.OBE = 0;
    R_GPT06_1->GTIOR_b.OAE = 0;  R_GPT06_1->GTIOR_b.OBE = 0;
    R_GPT06_2->GTIOR_b.OAE = 0;  R_GPT06_2->GTIOR_b.OBE = 0;
    R_GPT07_0->GTIOR_b.OAE = 0;  R_GPT07_0->GTIOR_b.OBE = 0;
    R_GPT07_1->GTIOR_b.OAE = 0;  R_GPT07_1->GTIOR_b.OBE = 0;
    R_GPT07_2->GTIOR_b.OAE = 0;  R_GPT07_2->GTIOR_b.OBE = 0;
    R_GPT08_0->GTIOR_b.OAE = 0;  R_GPT08_0->GTIOR_b.OBE = 0;
    R_GPT08_1->GTIOR_b.OAE = 0;  R_GPT08_1->GTIOR_b.OBE = 0;
    R_GPT08_2->GTIOR_b.OAE = 0;  R_GPT08_2->GTIOR_b.OBE = 0;

    /* DSM 設定（已確認不會干擾 Linux） */
    setup_dsm();

    /* 啟動三相 PWM（真正讓馬達能轉的地方） */
    R_GPT_THREE_PHASE_Start(&g_three_phase0_ctrl);
    R_GPT_THREE_PHASE_Start(&g_three_phase1_ctrl);
    R_GPT_THREE_PHASE_Start(&g_three_phase2_ctrl);
    R_GPT_THREE_PHASE_Start(&g_three_phase3_ctrl);
    R_GPT_THREE_PHASE_Start(&g_three_phase4_ctrl);
    R_GPT_THREE_PHASE_Start(&g_three_phase5_ctrl);
    R_GPT_THREE_PHASE_Start(&g_three_phase6_ctrl);
    R_GPT_THREE_PHASE_Start(&g_three_phase7_ctrl);
    R_GPT_THREE_PHASE_Start(&g_three_phase8_ctrl);

    /* UART0：保留，讓原本 Windows 工具仍可用 */
    R_SCI_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);
    R_SCI5_Create();

    /* 下列項目全部不要在 remoteproc 模式做：
       - R_IOPORT_Open(&g_ioport_ctrl, &g_bsp_pin_cfg);
       - R_BSP_RegisterProtectDisable(...)
       - R_RWP_NS->PRCRN / R_RWP_S->PRCRS
       - R_SYSC_NS->MSTPCRD_xxx / MSTPCRA_xxx
       - OSPI / xSPI reset & DTR 設定 */
}

```
> 建議在函式前面加註解：「此版本用於 Linux remoteproc，不可再做 clock/reset/pinmux」。

----------

## 6. 修改 m_rzt.c：m_startup_remoteproc_safe()

### 6.1 目的

-   原本 `m_startup()` 會做很多初始化，同時可能改到系統狀態。
-   建立 `m_startup_remoteproc_safe()`：
    -   避開會讓 Linux 掛掉的東西
    -   只做「motor 控制必要的資料結構初始化」
    -   呼叫 `setup_motor()`、`setup_encoder()`
        

### 6.2 建議實作骨架

在 `m_rzt.c` 裡增加：
```c
void m_startup_remoteproc_safe(void)
{
    /* 設定 motor index */
    for (uint32_t idx = 0; idx < MOTOR_NUM; idx++)
    {
        g_st_m[idx].motor_idx = idx;
    }

    /* GPT Phase / PWM Period register 指標初始化 */
    g_st_m[0].regPhaseU    = (unsigned int *)&(R_GPT00_0->GTCCR[2]);
    g_st_m[0].regPhaseV    = (unsigned int *)&(R_GPT00_1->GTCCR[2]);
    g_st_m[0].regPhaseW    = (unsigned int *)&(R_GPT00_2->GTCCR[2]);
    g_st_m[0].regPWMPeriod = (unsigned int *)&(R_GPT00_0->GTPR);

    /* ... 1~8 顆馬達依原有程式填滿 ... */

    /* Encoder / MTU1 相關暫存器指標初始化 */
    for (uint32_t idx = 0; idx < MOTOR_NUM; idx++)
    {
        g_st_m[idx].regPosCounter = (unsigned short *)&(R_MTU1->TCNT);
        g_st_m[idx].regPosCapture = (unsigned short *)&(R_MTU1->TGRA);
        g_st_m[idx].regTimerStatus = (unsigned char *)&(R_MTU1->TSR);

        g_st_m[idx].flash_pars_offs = 0xA00000 + 0x10000 * g_st_m[idx].motor_idx;
        g_st_m[idx].enc_open        = 0;
        g_st_m[idx].slave           = &m2;
        g_st_m[idx].module_addr     = 1;
        g_st_m[idx].group_addr      = 0;
        g_st_m[idx].pvt_watermark   = 10;
        g_st_m[idx].pvt_period      = 200;
        setup_motor(&g_st_m[idx], 0);
    }

    /* LED1~5 如有使用、且腳位與 Linux pins 沒衝突才開啟；
       目前實測是註解掉 LED 才不會影響 MMC，
       建議 remoteproc 版本一律先關掉 LED 控制： */

#if 0
    LED1 = 1;
    LED2 = 0;
    LED3 = 0;
    LED4 = 0;
    LED5 = 0;
#endif

    for(uint32_t  idx  =  0;  idx  < MOTOR_NUM;  idx++)
    {
        setup_encoder(&g_st_m[idx], g_st_m[idx].encoder_type);
    }
}

```
> 重點：
> 
> -   `setup_motor()` / `setup_encoder()` 在 remoteproc 模式是 **必要** 的，不然馬達不會真的動。
>     
> -   LED 腳位容易跟 Linux 使用的 pinmux 衝突，建議 remoteproc 版先關閉。
>     
> -   所有 direct register mapping（GPT、MTU）都沿用原專案，不額外更動。
>     

----------

## 7. Build 專案並輸出 ELF

1.  在 e² studio 選擇新專案 `*_remoteproc`   
2.  右鍵 → **Build Project**
3.  成功後，到 `Debug` 或 `Release` 資料夾找到：
    -   `RZT2H_INVBLV_SPM_ENCD_FOC_E2S_V100_remoteproc.elf`
4.  將該檔拷貝到 Linux 板子（例如 `/lib/firmware`或）。
    

----------

## 8. Linux 端操作 remoteproc + 驗證

### 8.1 啟動 CR52 韌體
```sh
echo RZT2H_INVBLV_SPM_ENCD_FOC_E2S_V100_remoteproc.elf \
  > /sys/class/remoteproc/remoteproc0/firmware

echo start > /sys/class/remoteproc/remoteproc0/state
```
確認狀態：
```sh
cat /sys/class/remoteproc/remoteproc0/state
# 應該看到 "running"
```
### 8.2 確認 CR52 主迴圈有在跑

在 `m_heartbeat()` 中有：
```sh
_cntr++;
if (_cntr >= 10000)
{
    _cntr = 0;
    // LED1 ^= 1; // remoteproc 版先關掉
}
```
也可以額外寫某個 debug counter 在 0x10070020：
```c
(*(volatile  uint32_t*)0x10070020)++;
```
Linux 上讀值：
```sh
sudo devmem2 0x10070020
# 連續讀，看到數字一直增加 => CR52 正常跑
```
----------

## 9. 常見問題與排查

### 9.1 啟動後 MMC 出現 -84 / I/O error / EXT4 error

檢查以下幾點：

1.  `hal_entry.c` 是否仍有：
    
    -   `R_RWP_NS->PRCRN` / `R_RWP_S->PRCRS`
    -   `R_SYSC_NS->MSTPCRA*` / `MSTPCRC*`
        
2.  `R_Systeminit()` 是否還呼叫：
    
    -   `R_IOPORT_Open(&g_ioport_ctrl, &g_bsp_pin_cfg);`
    -   `R_BSP_RegisterProtectDisable()` + `MRCTLA` / `MSTPCR*`
    -   OSPI / xSPI 相關設定
        
3.  是否有 LED 控制腳位正好跟 MMC pins 共用：
    -   若懷疑，先把所有 `LEDx =` 相關程式碼整段註解。
        
