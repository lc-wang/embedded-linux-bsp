# RK3588 Android 15 MIPI Display Rotation Technical Report

## 1. 問題概述（背景與問題描述）

RK3588 平台使用 MIPI 訊號驅動第二顆外接顯示器（DisplayId=2）。

目標：
-   **將 MIPI 面板旋轉 90°（或 270°）**
-   **內容完全填滿、不裁切、不壓扁**
-   **行為與 AOSP `wm user-rotation` 一致**
    
問題：

-   **Android 15 的 rotation 流程已與 A14 差異極大**    
-   Rockchip BSP 過去 patch（A14）在 A15 上幾乎無法直接套用 
-   init.rc → `wm user-rotation` 無效 + SELinux 阻擋 
-   SurfaceFlinger 調整 projection → 畫面會裁切 / 錯位
-   LogicalDisplay 修改各種 hack → 還是少一整列 icon 

## 2. 除錯過程

### 2.1 Android 15 顯示架構變動

Android 15 對 multi-display 的變動相當大，以下為與 Android 14 的差異比較：

| 項目 | Android 14 | Android 15 |
|------|------------|------------|
| DisplayRotation 管理 | 固定路徑 | 抽到 WindowManager 更上層 + Input thread 整併 |
| LogicalDisplay 行為 | 比較單純 | 多 display 互相同步，邏輯更複雜 |
| DisplayArea | 只有一層 | 多種 WindowArea，輸出邏輯大幅變動 |
| setProjection | 主要由 SurfaceFlinger 控制 | 減少直接操作，改由 WindowManagerService 統合管理 |
| 視窗 inset / navigationInset | 計算方式較簡單 | 對外接螢幕也套用，計算變得更複雜 |

此架構變動 → **Rockchip A14 Patch 在 A15 會錯位 & 被裁切**。

### 2.2 研究方法與實驗流程

1.  **比對 A14 / A15 Rockchip BSP patch**
2.  逐檔案分析：
    -   LocalDisplayAdapter     
    -   LogicalDisplay
    -   DisplayDevice
    -   SurfaceFlinger
3.  測試所有可能的修改點
4.  用 `adb logcat -s SurfaceFlinger` 追動畫面
5.  dumpsys SurfaceFlinger → 分析 MIPI display 投影矩陣
6.  A/B 測試不同修改

### 2.3 為何 `wm user-rotation -d 2 lock 1` 是唯一有效的？

因為它完整觸發了 Android 正常旋轉流程：

```sql
wm → cmd window → WindowManagerService.setUserRotation
→ updateRotationUnchecked
→ DisplayRotation # per-display 修改
→ SurfaceFlinger.setProjection
→ HAL
``` 

這條路徑會：

✓ 正確重新計算 Insets  
✓ 正確更新 LogicalDisplay geometry  
✓ 正確通知 SurfaceFlinger 更新投影矩陣  
✓ 正確更新 Input system（touch rotation）

任何跳過 WMS 的方法（如直接改 SF）都會錯亂 → 導致裁切。

### 2.4 init.rc 嘗試與為何失敗

嘗試：

```bash
on boot_completed
    exec ... wm user-rotation -d 2  lock  1
```

但 log 顯示：
```bash
exit 127
neverallow
system_server_service denied
``` 
#### 原因 1：wm 需要 shell PATH，init 沒有
#### 原因 2：wm 需要 binder IPC，init 執行時 system_server 還沒 ready
#### 原因 3：exec 對象被 SELinux 阻擋
#### 原因 4：Android 15 WMS 更嚴格，不接受 early rotation call
#### 結論：
**init.rc 無法做 per-display rotation。**  
唯一方法：**進 framework。**

### 2.5 Rockchip 原廠 Patch（A14 vs A15）比較

| 功能 | Android 14 | Android 15 |
|--------|------------|------------|
| per-display `einit-1` | ✓ 有效 | ✗ 無效 |
| per-display `efull-1` | ✓ 有效 | ✗ 無效 |
| LocalDisplayAdapter | 覆寫 rotation | A15 行為改變 |
| LogicalDisplay | 覆寫 appWidth/Height | A15 仍會被後面 override |
| ContentRecorder | Mirror path 正常 | A15 改 Mirror 路徑，對實體顯示無效 |

結論：

#### Rockchip A14 patch 無法直接移植到 A15

因為抽象層全部重新設計。

### 2.6 Android 15 真正的 rotation 流程

對外接顯示器：
```scss
WindowManagerService
    → DisplayContent
    → DisplayRotation
    → DisplayPolicy (Insets)
    → LogicalDisplay
    → SurfaceFlinger.setActiveConfig()
    → SurfaceFlinger.setProjection()
```
必須全部走過，才能：
-   避免裁切  
-   同步 scale    
-   同步 touch alignment   
-   同步 system bar inset

### 2.7 dumpsys SurfaceFlinger 深度分析（DisplayId=2）
dumpsys 證實：

-   mOrientedDisplaySpace 是正確的   
-   layerStackRect 正確
-   **DisplayViewport 與 DisplayFrame 不一致**  
右側裁切的原因：

#### 計算 inset 前後發生 race condition

LogicalDisplay 設定的 geometry 被 InputFlinger / InsetsPolicy 覆蓋。

這證實：  
✓ 必須走 WMS 正規種子流程  
✗ 不能只 patch SF / LogicalDisplay

## 3. Root Cause 分析

displayId=2 出現：
-   **右側少一整列 icon**
-   **畫面向左下偏移**
-   **SurfaceFlinger 計算的投影矩陣與預期不一致**
其根因是：

### 3.1 Android 15 的多顯示器旋轉邏輯完全倚賴 WMS + DisplayRotation 的 notification chain

如果在 SF / LogicalDisplay 直接 override：
#### 後果

- Insets 不會同步更新  
- layout stack 與 display stack 不一致  
- WindowManager 認為尚未旋轉，但 SurfaceFlinger 已旋轉（兩者不同步）  
- SurfaceControl Transaction 中 Matrix 計算異常  
- **最終畫面一定裁切、方向錯誤**  

## 4. 解決方案（正式解法：加入 framework-level「開機後旋轉」）

後來成功的方案：

### 4.1 在 WindowManagerService.systemReady 呼叫：

`setUserRotation(displayId, USER_ROTATION_LOCKED, rotation);` 

這會：
-   強制啟動完整 rotation chain
-   完整更新 Insets
-   更新 Touch orientation 
-   更新 fullscreen geometry
-   不會裁切
-   不會偏移

這也是 `wm user-rotation` 正常的原因  
把 wm 的邏輯搬到 framework 內自動執行！

## 5. 結論與建議

### 5.1 不推薦的錯誤方向

| 方法 | 為何不能用 |
|-------|-------------|
| 修改 SurfaceFlinger `setProjection` | 與 WindowManager 不一致 → 畫面裁切 |
| 修改 `DisplayDevice.setProjection` | 會被 LogicalDisplay 再次 override |
| 修改 LogicalDisplay `layerStackRect` | InsetsPolicy 會覆蓋掉所有變更 |
| 在 init.rc 執行 `wm` 指令 | SELinux 限制 + system_server 尚未準備好 |
| 使用 ContentRecorder hack | 這是錄影鏡像功能，不會改變真實顯示 |

### 5.2 結論

✓ Android 15 的外接顯示器 rotation 必須走 **WindowManagerService 正規流程**  
✓ init.rc 無法設定 per-display rotation  
✓ Rockchip A14 patch 無法直接套用於 A15  
✓ 正確做法：  
**在 WMS boot 完成後，自動呼叫 setUserRotation(displayId)**

這是：
-   不裁切
-   不偏移
-   scale 正確
-   Insets 正確
-   Touch rotation 正確
的方法。

### 5.3 未來可維護性建議

-   建一個 **RotationService**，集中處理外接顯示設定 
-   改用 **system_ext overlay property** 控制 per-display rotation
-   預留後續 Android 16 的可能 API 變動
-   將 MIPI panel rotation 配置加入 device overlay（config.xml）

## 附錄

### A. 指令
```sql
adb shell wm user-rotation -d 2 free
adb shell wm user-rotation -d 2 lock 1
adb shell dumpsys SurfaceFlinger > sf.txt
adb logcat -s SurfaceFlinger
```
