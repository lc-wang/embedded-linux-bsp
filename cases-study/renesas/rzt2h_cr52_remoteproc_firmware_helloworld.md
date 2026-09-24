# RZ/T2H CR52 + Linux remoteproc HelloWorld 教學

## 1. 目標

本文件說明如何在 **Renesas RZ/T2H** 平台上，使用 **e² studio** 建立一個最小化的 CR52 firmware（HelloWorld），並由 **Linux remoteproc** 在 A55 端載入與啟動。

> 本教學 **不包含 OpenAMP / RPMsg**，僅示範：
> - 如何產生一顆可被 remoteproc 啟動的 CR52 ELF
> - 如何在 Linux 上透過 remoteproc 啟動 CR52
> - 如何用 `devmem2` 驗證 CR52 firmware 確實在執行

## 2. 環境與前置條件

### 2.1 軟體環境

- e² studio（安裝對應 RZ/T2H FSP 套件）
- ARM GCC 工具鏈（隨 e² studio 安裝）
- Linux：
  - Kernel 已啟用 `renesas,rz-cr52` remoteproc driver
  - DTS 已加入 CR52 remoteproc 節點及 `renesas,rz-start_address`
  - `/sys/class/remoteproc/remoteproc0` 存在
- 使用者空間工具：
  - `devmem2`（可自行編譯或使用發行版套件）

### 2.2 硬體環境

- RZ/T2H Evaluation Board（例如：R9A09G077M44）
- A55 端 Linux 可正常啟動並登入 root shell

## 3. e² studio 專案建立

### 3.1 建立新專案

1. 開啟 **e² studio**
2. 選擇：
   - `File` → `New` → `Renesas C/C++ Project`
3. 在裝置選擇頁：
   - Device：選擇 **R9A09G077M48GBG**（或實際使用的 RZ/T2H 型號）
4. Project Type 建議：
   - Baremetal / Standalone
   - 不使用 RTOS（或後續不啟用）
5. 完成精靈流程，建立專案。

建立後，專案中會包含：

- `src/hal_entry.c`
- 可能包含 `src/main.c`
- FSP 自動產生的設定與啟動碼（後續會關閉 FSP 的 startup，改用自訂 startup）

假設最終目錄結構如下：

```text
<project_root>/
  src/
    hal_entry.c
    startup_cr52.S      ← 稍後新增
  script/
    rzt2h_cr52_remoteproc.ld  ← 稍後新增
 ```

## 4. 新增自訂 linker script（remoteproc 專用）

為了配合 Linux remoteproc 的啟動位址，CR52 firmware 需要對齊 DTS 中設定的 `renesas,rz-start_address`。以下示範使用：

-   CR52 SRAM 範圍：`0x10000000 - 0x101FFFFF`（2MB）
    
-   remoteproc start_address：`0x10061000`
    
### 4.1 新增檔案

1.  在 e² studio 專案上按右鍵：
    
    -   `New` → `Folder` → 建立資料夾：`script`
        
2.  在 `script` 上按右鍵：
    
    -   `New` → `File` → 檔名輸入：`rzt2h_cr52_remoteproc.ld`
        
### 4.2 貼上 linker script 內容

在 `script/rzt2h_cr52_remoteproc.ld` 中貼入：

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

/* Provide to assembly */
_estack = ORIGIN(FW) + LENGTH(FW);

SECTIONS
{
    /* === vectors + _start === */
    .vectors :
    {
        KEEP(*(.vectors))
    } > FW

    /* === Text and Read-Only Data === */
    .text :
    {
        *(.text*)
        *(.gnu.linkonce.t.*)

        /* ARM unwind tables (must be inside loadable region) */
        *(.ARM.extab*)
        *(.ARM.exidx*)

        /* Read-only data */
        *(.rodata*)
        *(.gnu.linkonce.r.*)
    } > FW

    /* === C Init Arrays (constructor lists) === */
    .init_array :
    {
        KEEP(*(.init_array*))
    } > FW

    .fini_array :
    {
        KEEP(*(.fini_array*))
    } > FW

    /* === Data === */
    .data :
    {
        _sdata = .;
        *(.data*)
        *(.gnu.linkonce.d.*)
        _edata = .;
    } > FW

    /* === BSS === */
    .bss (NOLOAD) :
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        __bss_end__ = .;
    } > FW

    /* === Stack === */
    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _stack_bottom = .;

        /* stack size 8KB (可依需求調整) */
        . = . + 0x2000;

        _stack_top = .;
    } > FW

    .resource_table :
    {
        KEEP(*(.resource_table*))
    } > FW
}
```
注意：這裡僅預留 .resource_table 區段，但本教學的 HelloWorld firmware 不實際使用 resource table。remoteproc 會顯示 no resource table found for this firmware，屬於正常現象。

## 5. 新增 CR52 startup 檔（_start 入口）

Linux remoteproc 會將 PC 設為 `0x10061000`，因此需要在該位址放入有效的向量／入口點 `_start`，並設置 stack pointer，然後跳轉到 C 程式。

### 5.1 新增檔案

1.  在 `src` 目錄上按右鍵：
    
    -   `New` → `File` → 檔名：`startup_cr52.S`
        
### 5.2 貼上 startup 程式碼（簡化版）
```sh
    .syntax unified
    .cpu cortex-r52
    .fpu neon-fp-armv8

    .section .vectors, "ax"
    .global _start
    .type _start, %function

/* Reset vector for CR52, used by Linux remoteproc */
_start:
    /* 切換到 ARM 狀態（確保非 Thumb） */
    .arm

    /* 設定 stack pointer
     * _estack 由 linker script 提供：
     *   _estack = ORIGIN(FW) + LENGTH(FW);
     */
    LDR   sp, =_estack

    /* 跳到 C 進入點 main() */
    BL    main

1:
    B     1b   /* main() 返回時停在這裡 */
 ```

## 6. 關閉 FSP 預設 startup（如果存在）

有些 FSP 專案會自動產生 `startup.c` / `startup_core.c` 等啟動檔，內容會自行設定向量表與 C runtime。  
這類檔案不應與自訂 `startup_cr52.S` 同時存在。

處理方式：

1.  在專案的 `src/` 或 `rzt_gen/` 目錄中，尋找類似檔案：
    
    -   `startup.c`
        
    -   `startup_core.c`
        
    -   其他包含向量表與重置處理的檔案
        
2.  對每個檔案：
    
    -   右鍵 → `Properties`
        
    -   `C/C++ Build` → `Settings`
        
    -   勾選 **"Exclude resource from build"**
        
3.  如果專案中不存在這些檔案，則可略過此步驟。
    
## 7. 實作 HelloWorld firmware：`hal_entry.c`

目標行為：CR52 firmware 週期性向一個固定位址寫入數值，A55 端可以用 `devmem2` 看到數值不斷增長。

### 7.1 修改 `src/hal_entry.c`

將 `hal_entry.c` 改成以下內容：

```c
#include <stdint.h>

/* 共用測試位址：
 *  - 此位址位於 CR52 與 A55 皆可看到的記憶體範圍
 *  - 實務中可依實際記憶體配置調整
 */
#define TEST_ADDR (*(volatile uint32_t *)0x10070000U)

/* e² studio/FSP 預設會在 main() 中呼叫 hal_entry()
 * 本範例直接在此放入無窮迴圈邏輯。
 */
void hal_entry(void)
{
    uint32_t counter = 0;

    while (1)
    {
        TEST_ADDR = 0x12340000U + counter;
        counter++;

        /* 簡單 busy-wait 延遲，避免更新太快難以觀察 */
        for (volatile uint32_t i = 0; i < 500000U; i++)
        {
            __asm volatile ("" ::: "memory");
        }
    }
}
```
若 main.c 存在，通常 FSP 預設內容會在 main() 呼叫 hal_entry()，無需修改。

## 8. 設定專案使用自訂 linker script

1.  專案按右鍵 → `Properties`
    
2.  左側選擇：
    
    -   `C/C++ Build` → `Settings`
        
3.  在 Tool Settings 中找到 **Linker** 設定（名稱可能類似）：
    
    -   例如：`Cross ARM C Linker` 或 Renesas toolchain 對應項目
        
4.  尋找 Script / Command line 選項：
    
    -   將 `-T` 參數指向自訂 linker script，例如：

```sh
-Tscript/rzt2h_cr52_remoteproc.ld
```
套用後關閉設定視窗。

## 9. 編譯專案並產生 ELF

1.  在 e² studio：
    
    -   `Project` → `Clean`
        
    -   `Project` → `Build Project`
        
2.  確認輸出：
    
    -   在例如 `Debug/` 或 `Release/` 目錄下，產生：

```sh
HelloWorld.elf
```

若編譯出現錯誤，優先檢查：

-   是否有多個 startup 檔未排除
    
-   linker script 路徑是否正確
    
-   `_start` 是否有定義（`startup_cr52.S` 有 `global _start`）

## 10. Linux 端：透過 remoteproc 啟動 CR52

以下假設：

-   CR52 對應 device 為 `/sys/class/remoteproc/remoteproc0`
    
-   firmware 路徑為 `/lib/firmware/HelloWorld.elf`
    
### 10.1 確認 remoteproc device 存在
```sh
ls /sys/class/remoteproc
# 預期看到 remoteproc0
```
如設備編號不同（例如 remoteproc1），後續請相應替換路徑。

### 10.2 放置 firmware
```sh
sudo cp HelloWorld.elf /lib/firmware/
```
### 10.3 設定 firmware 名稱
```sh
echo HelloWorld.elf | sudo tee /sys/class/remoteproc/remoteproc0/firmware
```
### 10.4 啟動 CR52
```sh
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
```
### 10.5 查看 kernel log
```sh
dmesg | tail -n 20
```
預期看到類似訊息：
```sh
remoteproc remoteproc0: powering up cr52_0
remoteproc remoteproc0: Booting fw image HelloWorld.elf, size 8504
remoteproc remoteproc0: no resource table found for this firmware
remoteproc remoteproc0: remote processor cr52_0 is now up
```
`no resource table found for this firmware` 在本教學中屬正常現象，因為 HelloWorld 未使用 resource table。

## 11. 驗證（CR52 firmware 是否在執行）

使用 `devmem2` 讀取 `0x10070000`，應該會看到數值持續變化。

### 11.1 安裝 devmem2（如尚未安裝）

例如在 Debian/Ubuntu：

```sh
sudo apt-get install devmem2
```
或自行下載原始碼編譯。

### 11.2 持續讀取測試位址
```sh
sudo devmem2 0x10070000
sudo devmem2 0x10070000
sudo devmem2 0x10070000
```
若看到輸出類似：

```sh
Value at address 0x10070000 (0xffff8aa34000): 0x12341C62
Value at address 0x10070000 (0xffff9670c000): 0x12341C77
Value at address 0x10070000 (0xffff9abcd000): 0x12341C8B
```

代表：

-   CR52 firmware 正在執行 `hal_entry()` 內的迴圈
    
-   remoteproc 已成功啟動 firmware 並讓 CR52 持續跑

## 12. 常見問題與排查

### 12.1 `Boot failed: -22` 或 `bad phdr da ...`

可能原因：

-   linker script 未對齊 `0x10061000`
    
-   `_start` 未正確放在 `.vectors` 區段
    
-   startup 檔未使用 ARM mode
    
建議：

-   確認 `ENTRY(_start)` 是否存在
    
-   確認 `.vectors` 區段位於 `FW (rxw)` 區內
    
-   確認 `startup_cr52.S` 第一個指令是 `.arm` 狀態
    
### 12.2 `Boot failed: -12` 或 `Registered carveout doesn't fit len request`

此教學的 HelloWorld 未使用 resource table。  
若手動加入 resource table 但未與 DTS 對齊，可能出現：

```sh
remoteproc remoteproc0: Registered carveout doesn't fit len request
remoteproc remoteproc0: Boot failed: -12
```

處理方式：

-   HelloWorld 階段建議 **完全不要放 resource table**，維持本教學提供的狀態。
    
-   若需要 OpenAMP / RPMsg，應以官方 OpenAMP 範例專案為基準，另行整合。

### 12.3 CR52 似乎無動作，但 remoteproc 顯示已啟動

建議檢查：

-   `hal_entry()` 是否確實被呼叫：
    
    -   若有 `main.c`，確認其中有呼叫 `hal_entry()`
        
-   `TEST_ADDR` 是否位於 CR52 實際可存取的記憶體範圍
    
-   `devmem2` 使用的位址是否一致（0x10070000）

## 附錄

### A. 停止 / 重新啟動 CR52

如需停止 CR52：
```sh
`echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state`
```
重新啟動：
```sh
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
```
若修改 firmware 後重新部署：
```sh
sudo cp HelloWorld.elf /lib/firmware/ echo stop  | sudo tee /sys/class/remoteproc/remoteproc0/state
```
