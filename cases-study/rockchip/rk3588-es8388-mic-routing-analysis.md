
# RK3588 + ES8388 麥克風切換問題技術報告

## 1. 問題背景

本系統使用：

-   **SoC**：Rockchip RK3588
-   **Audio Codec**：ES8388
-   **麥克風配置**：
    -   獨立板上麥克風（Main Mic）  
    -   耳麥麥克風（Headset Mic）

需求目標：

> 希望能透過 `amixer` 在 **獨立 Mic 與 耳麥 Mic 之間做「互斥切換」**，  
> 也就是同一時間 **只錄到其中一支麥克風**。

----------

## 2. 軟體與實驗環境

-   Linux ALSA / ASoC
-   使用 `amixer -c 1` 操作 ES8388 mixer
-   實際驗證方式：
    `arecord -D hw:1,0 -f S16_LE -r 48000 -c 2 | aplay` 
----------

## 3. ES8388 麥克風前端基本架構

### 3.1 類比輸入的三種型態


## 3. ES8388 麥克風前端基本架構

### 3.1 類比輸入的三種型態

| 類型       | 名稱                          | 特性說明                                   |
|------------|-------------------------------|--------------------------------------------|
| 單端輸入   | Line1 / Line2 / MicL / MicR   | 一條訊號線搭配 GND，架構簡單但易受雜訊影響 |
| 差分輸入   | MIC_P / MIC_N                 | 兩條訊號線取差值，具較佳抗雜訊能力          |
| PGA        | Programmable Gain Amplifier   | ADC 前的類比放大器，用於調整麥克風增益     |

----------

### 3.2 重要控制項對應意義

| Mixer 控制項            | 實際意義說明                                           |
|-------------------------|--------------------------------------------------------|
| Main Mic Switch         | 開 / 關獨立麥克風的 Mic Bias 與 Enable                 |
| Headset Mic Switch     | 嘗試控制耳麥的 Mic Bias（不一定在所有板子上有效）     |
| Left / Right Line Mux  | 選擇單端輸入來源，或切換至 NC 以斷開輸入               |
| Left / Right PGA Mux   | 決定 PGA 的輸入來源（Line 或 Differential）           |
| Differential Mux       | 選擇差分輸入的來源群組                                 |

----------

## 4. 關鍵名詞解釋

### 4.1 Line 1L / Line 2L 是什麼？

> **左聲道的「單端類比輸入腳位」**
-   單端 = 訊號對地（Signal + GND）
-   常用於：
    -   耳麥麥克風        
    -   Line-in        
-   設為 `NC` 代表：
    -   **電氣上完全斷開**
    -   不再可能有該路徑訊號進入 PGA

----------

### 4.2 MIC_P / MIC_N 是什麼？

> **Codec 的「差分麥克風輸入腳位」**
-   Codec 量的是：   
    `MIC_P − MIC_N` 
-   優點： 
    -   抗雜訊（CMRR）  
    -   適合板上獨立麥克風

重點：

> **MIC_P / MIC_N 是一對「輸入端子」  
> 不是某一支特定麥克風**

----------

## 5. 實際驗證結果與推論

### 5.1 已確認的事實（由 amixer 證實）

1.  設定以下條件後：
```bash
Left Line Mux = NC
Right Line Mux = NC
Headset Mic Switch = off
Left/Right PGA Mux = Differential` 
 ```
2.  耳麥 **仍然可以錄到聲音**
    

----------

### 5.2 這代表什麼？

> **耳麥麥克風並不是走 Line / MicL / MicR**

而是：

-   **耳麥的訊號在硬體上已經掛到 MIC_P / MIC_N**
-   或至少在類比前端與 Main Mic 匯流
    

也就是：
```yaml
MIC_P / MIC_N
 ├─ 獨立 Main Mic
 └─ 耳麥 Mic（經 jack / 電阻 / AC-coupling） 
```
----------

## 6. 為什麼「獨立 Mic 可以關，耳麥卻關不掉？」


### 6.1 原因解析：麥克風為什麼能 / 不能關

| 麥克風類型 | 為什麼能 / 不能關                                       |
|------------|----------------------------------------------------------|
| 獨立 Mic   | 需要由 Codec 提供 Mic Bias → 關閉 Main Mic Switch 即無訊號 |
| 耳麥 Mic   | Bias 可能來自外部，或已接在差分輸入端 → 不受該 Switch 控制 |

**共用的是「類比輸入端」  
不是「每支 mic 的供電開關」**

----------

## 7. 為什麼只下這兩條不夠？
```bash
amixer -c 1 cset name='Headset Mic Switch' off
amixer -c 1 cset name='Main Mic Switch' on
```
因為它們：
-   ✓ 只控制 mic 是否「活著」    
-   ✗ 不控制 mic 聲音是否「進 ADC」
    

真正決定錄音來源的是：
-   `Line Mux`
-   `PGA Mux`
-   `Differential Mux`
    
-   **以及硬體接線**

----------

## 8. 最終工程結論

### 在目前硬體設計下：

> **「插著耳麥，還想只錄 Main Mic」  
> 在純軟體層是做不到的**

不是 ALSA / amixer 問題  
而是 **類比電路已經把訊號混在一起**

----------

## 9. 可行策略

#
## 9. 可行且正確的產品級策略

### 建議行為

| 狀態       | 使用的麥克風                 |
|------------|------------------------------|
| 未插入耳麥 | Main Mic（Differential）     |
| 插入耳麥   | Headset Mic（Line）          |

這是 Android / Laptop / Notebook 的實際做法。

----------

## 10. 若一定要做到真正互斥

### 方法一：硬體修改（Board spin）

-   耳麥不要接到 MIC_P / MIC_N
-   或加入 analog switch（TS5A / FSA 系列）
    

### 方法二：重新定義產品需求

-   接受「插耳麥就用耳麥 mic」
    
----------

## 11. 最後總結

> **MIC_P / MIC_N 是「codec 的差分輸入端」  
> 不是某一支麥克風**
> 
> 只要兩支 mic 在這裡匯流，  
> **就沒有任何 amixer 能再把它們分開**
