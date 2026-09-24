# Trusted Firmware-A Debug Console 架構與開機流程分析

在 SoC 平台早期 bring-up 階段中，  
**UART debug console 往往是唯一可觀察系統狀態的介面**。

在尚未具備以下條件之前：

- DRAM 初始化完成
- MMU 啟用
- device tree 可用
- 作業系統載入
- JTAG debug 穩定

序列輸出（UART）通常是唯一能判斷系統是否仍在執行的依據。

因此，ARM Trusted Firmware-A（TF-A）在設計上，  
將 **debug console 視為最早期必須可用的基礎設施之一**。

本文目的在於整理：

- TF-A debug console 的整體架構
- console 初始化時序
- early console 與 runtime console 的差異
- 為何 console 需要在多個 stage 重複註冊
- TF-A 與 U-Boot 之間 UART 繼承關係的正確理解

本文不討論特定平台（例如 SCI0 → SCI1）的修改方式，  
而是建立一套 **可跨 SoC 使用的 TF-A console 心智模型**。

## 1. TF-A Console 架構概觀

TF-A 內建一套極簡化的字元輸出框架，用於支援早期除錯輸出。

其整體呼叫路徑如下：
```
tf_log()
│
▼
tf_printf()
│
▼
console_putc()
│
▼
| TF-A Console Framework |
│
└── UART backend（MMIO 存取）
```

與 Linux 或 U-Boot 不同，TF-A console 具有以下特性：

- 不使用 device tree
- 不使用 driver model
- 不支援 probe / remove
- 不使用動態記憶體
- 僅透過 MMIO 存取 UART 暫存器

此設計確保 console 能在極早期階段運作。

## 2. Console 資料結構

TF-A 中每一個 console instance 皆由 `console_t` 表示：

```c
typedef struct console {
    uintptr_t base;
    uint32_t flags;
    int (*putc)(int c, struct console *console);
    int (*getc)(struct console *console);
} console_t;
```

其意義如下：

-   `base`：UART MMIO base address

-   `putc()`：輸出單一字元

-   `getc()`：接收字元（非必要）

-   `flags`：console 可使用階段

TF-A console driver 的功能非常單純：

> **僅負責「把一個字元寫到 UART」。**

所有格式化輸出與 log 管理皆由上層處理。

## 3. Console Scope（輸出範圍）

每個 console 會註冊一組 scope flags：
```
CONSOLE_FLAG_BOOT
CONSOLE_FLAG_RUNTIME
CONSOLE_FLAG_CRASH
```

| Flag     | 說明                                   |
|----------|----------------------------------------|
| BOOT     | 開機階段輸出（BL2 / BL31）              |
| RUNTIME  | EL3 runtime service 輸出                |
| CRASH    | Exception / Panic 時的除錯輸出          |

若 scope 設定錯誤，常見症狀包括：

-   BL2 有 log，BL31 沒 log

-   MMU 開啟後 console 消失

-   panic 時完全無輸出

## 4. Console 初始化時序

TF-A 的 console 初始化流程如下：
```
Boot ROM
  │
  ▼
BL1（optional）
  │
  ▼
BL2
  │
  ├─ plat_early_platform_setup()
  │     └─ early console 註冊
  │
  ├─ bl2_main()
  │
  ▼
BL31
  │
  ├─ bl31_early_platform_setup()
  │     └─ console 重新註冊
  │
  ├─ bl31_platform_setup()
  │
  └─ bl31_main()
  │
  ▼
BL33（U-Boot）
```

## 5. 為何 console 必須重複註冊？

雖然 BL2 與 BL31 是連續執行的韌體階段，但它們：

-   是獨立 image

-   使用獨立 linker script

-   擁有各自的 `.bss` 與 global variable

因此：

`static  console_t console;` 

只存在於該 image 的記憶體空間中。

當控制權由 BL2 轉移至 BL31 時：

-   BL2 的 console 結構已不存在

-   BL31 必須重新註冊 console

否則將出現：

`BL2 有輸出
BL31 無輸出` 

此為 TF-A 正常設計行為。

## 6. Early Console 與 Runtime Console

### 6.1 Early Console

特性：

-   非常早期初始化

-   MMU 尚未啟用

-   僅可使用 identity mapping

-   通常僅支援 TX

用途包括：

-   DDR 初始化前除錯

-   clock / reset bring-up

-   platform early fault 排查

常見初始化位置：
```
plat_early_platform_setup()
bl31_early_platform_setup()
```

### 6.2 Runtime Console

特性：

-   clock 與 memory 已穩定

-   MMU 已啟用

-   適用於正常開機輸出

常見初始化位置：
```
bl2_platform_setup()
bl31_platform_setup()
```

## 7. Console Framework 與 UART 硬體的區別

理解 TF-A console 時，必須明確區分兩個層級。

### 7.1 TF-A Console Framework（不會被繼承）

-   屬於 TF-A image 的內部資料結構

-   包含 `console_t`、function pointer

-   隨 image 結束即消失

TF-A console framework **不會傳遞給 U-Boot**。

### 7.2 UART 硬體狀態（會被繼承）

TF-A platform code 通常會完成以下初始化：

-   module clock enable

-   pinmux 設定

-   reset deassert

-   baudrate divisor 設定

-   UART transmitter 啟用

上述操作會改變：

`SoC peripheral hardware state` 

該狀態在 BL31 → BL33 跳轉後仍會保留。

因此：

> 只要 UART 未被 reset 或 clock gate，  
> U-Boot 即可直接接手使用。

## 8. TF-A 與 U-Boot Console 的正確關係

TF-A：

-   建立 UART 硬體可用狀態

-   提供 early / secure 世界輸出

U-Boot：

-   重新 probe UART driver

-   使用相同 MMIO base

-   直接存取既有 UART 設定

因此常見現象為：
```
TF-A log 正常
↓
U-Boot log 立即接續
```
而非重新初始化 UART。

## 9. 可能導致 console 中斷的情況

UART 繼承行為可能在以下情況中失效：

-   TF-A 在 BL31 關閉 UART clock

-   peripheral reset 被重新 assert

-   U-Boot DTS 將 pinmux 改為 GPIO

-   TF-A 與 U-Boot 使用不同 UART IP

-   U-Boot driver 對 UART 重新初始化

## 10. TF-A Console 的責任邊界

TF-A console 的責任終點為：

`EL3 → BL33 handoff` 

之後：

-   UART 硬體由 BL33 接管

-   TF-A console framework 不再存在

-   log 行為完全取決於 U-Boot 設計
