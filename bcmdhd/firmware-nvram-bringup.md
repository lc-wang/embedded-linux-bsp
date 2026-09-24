# Broadcom bcmdhd (DHD) Wi-Fi Driver — Firmware, NVRAM, and Bring-up Flow

在 bcmdhd 架構中，**firmware 與 NVRAM 不是「資源檔」，而是「行為定義的一部分」**。

實務上常見的狀況包括：

- Driver probe 成功、interface 出現，但 **scan / connect 完全不動**
- STA 能連 2.4G，卻永遠找不到 5G
- AP mode 能起來，但 throughput 極低
- 不同板子用同一顆晶片，行為卻完全不同

**這些問題有極高比例不是 driver bug，而是 firmware / NVRAM / regulatory mismatch。**

## 1. bcmdhd Bring-up 的整體流程

### 1.1 高層初始化順序

```text
dhd_attach()
 └─ dhd_bus_attach()
     ├─ chip reset
     ├─ firmware download
     ├─ nvram download
     ├─ firmware boot
     ├─ preinit ioctls
     └─ dhd_bus_start()
 ```

對應檔案：

-   `dhd_linux.c`
    
-   `dhd_common.c`
    
-   `dhd_sdio.c` / `dhd_pcie.c`
    
### 1.2 Bring-up 成功 ≠ Wi-Fi 可用

Bring-up 只代表：

-   firmware 能跑
    
-   control channel 能通
    
-   interface 能註冊
    
**不代表：**

-   scan 一定成功
    
-   regulatory 正確
    
-   PA / RF 設定符合板子
    
## 2. Firmware（`.bin` / `.trx`）

### 2.1 Firmware 在 bcmdhd 中的角色

Firmware 負責：

-   MAC / MLME state machine
    
-   Scan / Auth / Assoc / Roam
    
-   Rate control / aggregation
    
-   Power save / WOWLAN
    
**Firmware 定義「Wi-Fi 怎麼運作」**

### 2.2 Firmware Download 流程

-   SDIO：
    
    -   透過 CMD53 block write
        
    -   時間長、對 timing 敏感
        
-   PCIe：
    
    -   透過 memory window / BAR
        
    -   較快，但 reset 成本高
        
**常見失敗點**

-   firmware 與 driver 版本不匹配
    
-   download 成功，但 boot hang
    
-   firmware 啟動但 event 不回
    
## 3. NVRAM：最容易被低估的關鍵

### 3.1 什麼是 NVRAM？

在 bcmdhd 中，NVRAM 通常是一份 **文字檔（key=value）**，包含：

-   Board-specific RF / PA 設定
    
-   晶振（xtal）參數
    
-   天線配置
    
-   Regulatory hint
    
**NVRAM ≈ 板級硬體描述（但不是 device tree）**

### 3.2 常見 NVRAM 參數類型

| 類型        | 影響說明             |
|-------------|----------------------|
| boardflags  | RF 路徑與功率配置    |
| xtalfreq    | 時脈穩定性           |
| pa*         | 發射功率相關參數     |
| aa*         | 天線配置             |
| regrev     | 區域法規限制         |

**錯一個值，Wi-Fi 不一定掛，但行為會「很怪」**

### 3.3 NVRAM Download 與套用時機

-   firmware boot 前下載
    
-   firmware 解析後，決定整體 RF 行為
    
-   driver **無法修正 NVRAM 錯誤**
    
## 4. Regulatory / CLM（法規與頻道）

### 4.1 為什麼 regulatory 在 bcmdhd 特別重要？

在 FullMAC 中：

-   頻道可用性
    
-   功率限制
    
-   DFS 行為
    
**全部由 firmware 決定**

Linux cfg80211 只能「被告知結果」。

### 4.2 CLM / regulatory blob

許多新一代 bcmdhd 會使用：

-   CLM blob（Closed-source regulatory database）
    
-   或 firmware 內建 regulatory table
    
常見問題：

-   國碼設定成功，但頻道仍被禁用
    
-   AP mode 起來，但 client 掃不到
    
-   5G / DFS 頻道永遠不可用
    
## 5. Preinit IOCTLs

### 5.1 什麼是 preinit ioctls？

在 firmware boot 後，driver 會送出一系列 iovar：

-   `country`
    
-   `mpc`
    
-   `roam_off`
    
-   `ampdu`
    
-   `frameburst`
    
**這些指令會「覆蓋 firmware 預設行為」**

### 5.2 順序的重要性

-   country 設定太晚 → scan 結果錯
    
-   power save 設定錯誤 → throughput 異常
    
-   roam 設定不一致 → 連線不穩
    
**順序錯誤 ≈ 行為錯誤**

## 6. 常見問題與排查

### 6.1 常見 Bring-up 故障模式

#### Interface 存在，但 scan 無結果

可能原因：

-   NVRAM regrev / boardflags 錯誤
    
-   CLM 不匹配
    
-   firmware 不支援該 band
    
#### 只能用 2.4G，5G 完全消失

-   NVRAM 天線設定錯
    
-   regulatory 限制
    
-   PA table 不完整
    
#### AP mode throughput 異常低

-   PA / power 設定錯誤
    
-   firmware 使用 fallback rate
    
-   AMPDU 被 disable
    
### 6.2 Debug Firmware / NVRAM

#### 第一優先確認事項

-   firmware 與 driver 是否為同一 vendor / release
    
-   NVRAM 是否對應實際板子
    
-   regulatory / country 設定是否成功
    
#### Debug 技巧

-   開啟 firmware console log（若支援）
    
-   比對不同板子的 NVRAM 差異
    
-   用最小化 NVRAM 測試行為變化
    
**NVRAM debug 是「比較法」，不是「單點修正」**

### 6.3 常見誤解澄清

-   ✗「Wi-Fi 掛了就是 driver bug」
    
-   ✗「同一顆晶片，用同一份 NVRAM 應該沒問題」
    
-   ✗「country code 設定成功就代表 regulatory 正確」
    
**bcmdhd 的 bring-up 是 firmware + NVRAM + bus 的整體工程**
