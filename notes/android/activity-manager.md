
# Android Activity Manager（AMS）系統解析

> 本章定位：
> 
> -   站在 **Android System / BSP Engineer** 視角，理解 ActivityManagerService（AMS）在系統中的「決策角色」
>     
> -   不是描述 Activity lifecycle API，而是解釋 **為什麼系統要這樣決定**
>     
> -   能實際用於 debug：App 被殺、ANR、背景卡頓、系統壓力異常
>     

----------

## 1. AMS 在 Android 系統中的角色

在 Android 架構中，**AMS 是整個系統的「行為裁判」**。

-   input-system：事件從哪來   
-   graphics-display：畫面怎麼顯示  
-   system-services：system_server 有哪些服務
    

**AMS 則負責決定：哪些 App / Process 值得被系統「活著對待」**。

核心觀念：

> Android 不是「App 中心」，而是「使用者體驗（UX）中心」。

AMS 的存在，就是為了確保：

-   使用者正在互動的 App 不會被干擾
-   系統在資源不足時能做出一致、可預期的選擇
    

AMS 位於：

```text
system_server
 └─ ActivityManagerService
```

它不是單一模組，而是 **串接多個 subsystem 的決策中樞**。

----------

## 2. Activity / Process Lifecycle 的真正決策點

大多數文件在談 Activity lifecycle 時，會從：

```text
onCreate → onStart → onResume → onPause → onStop
```

但對系統工程師來說，真正重要的是：

> **「誰在決定 lifecycle 何時發生？」**

答案不是 App，而是 AMS。

### 2.1 Activity lifecycle 是「結果」，不是「原因」

-   Activity callback 是 **AMS 決策後的通知**
-   App 無法要求：
    -   一定常駐
    -   一定不被 kill
        

AMS 依據的是：
-   使用者互動狀態
-   Window 可見性（來自 WMS）
-   系統整體資源壓力
    

### 2.2 Process lifecycle 比 Activity 更重要

在 AMS 的視角中：

-   **Process 才是資源管理單位**
-   Activity 只是 process 內的一種狀態

這也是為什麼：
-   Activity 可能被重建
-   Process 可能直接被 kill
    
----------

## 3. Process Importance 與 OOM Adj

### 3.1 為什麼 Android 需要 process 分級

Android 的核心假設：

> 記憶體永遠不夠，所有 process 都有被犧牲的可能。

AMS 會將 process 分類，例如：


| 類型        | 意義說明                         |
|-------------|----------------------------------|
| Foreground  | 使用者正在互動的行程             |
| Visible     | 使用者可見但未直接互動的行程     |
| Service     | 提供背景服務的行程               |
| Cached      | 最近使用過，但目前不重要的行程   |


這不是抽象概念，而是：

-   **直接影響是否會被 kill**  
-   **直接影響 OOM adj 值**
    

### 3.2 OOM Adj 是怎麼來的

-   OOM adj 由 AMS 計算
-   傳遞給 kernel / LMKD 作為參考
    

重點是責任分界：

| 元件   | 負責職責說明                         |
|--------|--------------------------------------|
| AMS    | 計算並維護 Process 的重要性與狀態    |
| LMKD   | 在記憶體壓力下主動選擇並執行 Kill     |
| Kernel | 最終負責實際的記憶體回收與釋放        |


**如果 App 被殺，不代表 App 有 bug**，可能只是系統做了正確選擇。

----------

## 4. ANR 的真正判斷流程

ANR（Application Not Responding）常被誤解成：

> App main thread 太慢

但實際上，ANR 是 **AMS 發出的系統級判斷**。

### 4.1 ANR 不是單一來源

AMS 監控多種類型的 ANR：

| ANR 類型        | 來源說明                         |
|-----------------|----------------------------------|
| Input ANR       | 由 InputDispatcher 偵測並回報     |
| Broadcast ANR   | 廣播處理逾時                     |
| Service ANR     | Service callback 執行逾時         |


### 4.2 為什麼 ANR 常是「系統壓力」的結果

常見情境：

-   CPU 被背景任務佔滿
-   system_server thread 被 block
-   記憶體回收導致延遲
    

結果是：

-   App 看起來「沒回應」
-   但真正慢的是系統路徑
    

**ANR 是系統健康指標，不只是 App 錯誤。**

----------
## 5. AMS 與 WMS 的核心連結：Task 作為共同抽象

> 本節補齊 ActivityManager（AMS）與 WindowManager（WMS）之間 **為什麼在架構上高度耦合、卻又職責分離** 的關鍵原因。

在 Android 中，AMS 與 WMS 不是各自獨立的管理者， 它們透過 **Task（任務）** 這個抽象，形成一條穩定的決策資料流。

----------

### 5.1 為什麼需要 Task 這個抽象

Android 面對的問題不是「管理 Activity」，而是：

> 使用者認知的是一個「操作流程」，而不是單一畫面。

例如：

-   點擊桌面 icon
-   進入 App
-   在多個畫面間切換
-   切到其他 App，再切回來

這個「流程單位」就是 **Task**。

----------

### 5.2 AMS 中的 Task（行為與生命週期單位）

在 AMS / ActivityTaskManager（ATMS）視角中：
-   Task 是一組 Activity 的集合
-   代表一個使用者操作流程（back stack）
-   是前景 / 背景、lifecycle 與 process 重要性的判斷基礎
    

AMS 關心的是：
-   哪個 Task 在前景？
-   哪個 Task 對應的 process 應該被視為重要？
-   Back 鍵應該作用在哪個 Task？

----------

### 5.3 WMS 中的 Task（顯示與可見性單位）

在 WMS 視角中：
-   Task 是一組 window / surface 的容器
-   決定畫面上的 z-order、可見性與 focus
-   支援 split-screen、PIP、多 display
    

WMS 關心的是：
-   哪個 Task 在最上層？ 
-   哪個 Task 對使用者可見？
-   哪些 window 屬於同一個 Task？

----------

### 5.4 關鍵點：AMS 與 WMS 操作的是「同一個 Task」

這是理解 Android 架構的關鍵：

> **AMS 與 WMS 並不是各自維護不同的 Task**  
> **而是對「同一個 Task」負責不同面向**。

可以用一句話概括：

-   WMS 提供 **Task 的可見性事實（visibility / focus）**
    
-   AMS 根據這些事實，做出 **系統層決策（importance / lifecycle）**
    

----------

### 5.5 實際決策資料流（Visibility → Importance）

以下是使用者切換 App 時，實際發生的流程：
```yaml
使用者切換畫面

↓

WMS：Task 可見性改變

↓

WMS 將狀態同步給 ATMS

↓

AMS：重新計算 Task / Process importance

↓

AMS：更新 OOM adj、前景 / 背景狀態
```
這條鏈路說明：

-   **WMS 是「眼睛」**：告訴系統現在看到什麼
-   **AMS 是「大腦」**：根據看到的結果做決定
    

----------

### 5.6 Task 為何是 Input 與 Graphics 的錨點

-   **Input**：   
    -   InputDispatcher 透過 WMS 判斷觸控落在哪個 Task 的 window
-   **Graphics**：
    -   SurfaceFlinger 合成的是一個個 Task 對應的 layer
-   **AMS**：
    -   根據 Task 是否可見，判斷其 process 是否重要

Task 是 Input / Graphics / AMS 之間的共同交集。

----------
## 6. Task、Process 與系統資源的責任邊界（AMS / LMKD / Kernel）

> 本節說明 **Task（使用者行為）如何映射到 Process（資源單位）**， 以及 AMS、LMKD、Kernel 之間各自負責的邊界。

理解這一節，才能回答一個核心問題：

> 為什麼「切到背景的 Task」會讓對應的 Process 更容易被殺？

----------

### 6.1 Task 與 Process 不是一對一

在 Android 架構中：

-   **Task 是使用者行為單位**（Activity / back stack）
-   **Process 是系統資源單位**（memory / CPU）
    

一個 Task：

-   可能只對應一個 process
-   也可能橫跨多個 process（service / content provider）
    

AMS 的責任不是維持 Task 與 Process 的一對一關係， 而是 **根據 Task 狀態，動態調整相關 process 的重要性**。

----------

### 6.2 可見 Task 如何影響 Process Importance

實際決策流程如下：
```yaml
Task 變為可見（前景）

↓

WMS 回報可見性狀態

↓

AMS 提升 Task importance

↓

AMS 提升對應 process 的 OOM adj / 前景等級
```
反之：
```yaml
Task 不可見（背景）

↓

AMS 降低 Task importance

↓

對應 process 進入 cached / background
```
**Task 是 process importance 的主要來源之一。**

----------

### 6.3 AMS 與 LMKD / Kernel 的責任分工

這三者常被混在一起討論，但實際責任非常清楚：

| 元件   | 負責內容                                   |
|--------|--------------------------------------------|
| AMS    | 計算並維護 Task / Process 的重要性與狀態   |
| LMKD   | 在記憶體壓力下選擇適合的 Kill 對象          |
| Kernel | 執行實際的記憶體回收與 Process Kill         |

關鍵觀念：

> **AMS 不會殺 process，它只是在提供「誰比較重要」的排序依據。**

----------

### 6.4 常見誤解與實務陷阱

#### 誤解 1：App 被殺 = App 有問題

實務上更常見的是：

-   Task 已不在使用者視野
-   系統記憶體壓力升高
-   Process 成為合理的回收對象
    

#### 誤解 2：OOM 問題一定是 Kernel

如果 AMS 的 Task / Process 分級錯誤：

-   即使 Kernel 正常
-   使用者仍會感覺「系統亂殺 App」
    

----------

### 6.5 Task 為何是記憶體與 UX 的橋樑

-   使用者只感知 **Task 是否還在**
-   系統只管理 **Process 是否還活著**
    
AMS 透過 Task， 把「使用者體驗」轉換成「系統資源決策」。

----------
## 6. AMS 與其他 subsystem 的互動

### 6.1 與 WindowManagerService（WMS）

-   WMS 提供 window 可見性
-   AMS 依此判斷 process 是否重要
    

沒有 WMS：
-   AMS 無法知道 App 是否真的在螢幕上
    

### 6.2 與 Input 系統

-   InputDispatcher 偵測 input timeout
-   回報 AMS
-   AMS 決定是否觸發 ANR
    

### 6.3 與 scheduler / cgroup（概念層）

-   AMS 不直接控制 scheduler
-   但它的「重要性決策」會影響：
    -   cgroup 分類
    -   資源分配策略

----------

## 7. Debug AMS 問題的工程方法

### 7.1 常用工具

```bash
dumpsys activity
dumpsys activity processes
dumpsys activity services
```

觀察重點：

-   process state
-   OOM adj
-   最近的 lifecycle 事件
    

### 7.2 常見誤判案例


| 現象       | 真正原因                           |
|------------|------------------------------------|
| App 被殺   | 系統記憶體壓力導致回收或 LMK 觸發   |
| App 卡頓   | system_server 關鍵 thread 被阻塞   |
| ANR        | Scheduler 延遲或 I/O 壓力累積       |


**先看系統狀態，再怪 App。**
