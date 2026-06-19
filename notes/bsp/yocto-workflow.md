
# Yocto BSP Workflow

> 目的：
> 
> -   建立一套「**修改有沒有真的進 image？**」的工程判斷流程  
> -   把 bitbake / layer / devtool 變成 **可預期、可 debug 的工具**

----------

## 1. Yocto 在 BSP 工程中的真實角色

Yocto 對 BSP 工程師來說，不是包裝工具，而是：

-   系統整合器（kernel / u-boot / rootfs） 
-   變更仲裁者（多個 layer 同時存在）
-   Debug 放大鏡（讓錯誤被隱藏或放大）
    

**工程現實**：

-   功能異常 ≠ code 有問題
-   很多時候是 **recipe / layer priority / override**

----------

## 2. Layer 模型：一切從這裡開始

### 2.1 Layer Priority 是真實世界的權力結構

```bash
bitbake-layers show-layers
```

判斷重點：

-   BSP layer 是否真的覆蓋 vendor layer
-   bbappend 有沒有被吃到
    

> **工程原則**：
> 
> -   不知道哪個 layer 生效，就不要 debug code
>     

----------

### 2.2 bbappend 是最容易「看起來存在，其實沒用」的東西

常見陷阱：

-   bbappend 檔名不匹配
-   layer priority 太低
-   FILESEXTRAPATHS 沒加
    

快速驗證：

```bash
bitbake -e virtual/kernel | grep ^FILE
```

----------

## 3. BSP 工程中三種「正確修改路徑」

### 路徑 A：devtool modify（短期 debug）

適用：

-   driver bring-up
-   快速驗證 patch    

```bash
devtool modify virtual/kernel

```

特性：

-   立即可用 
-   **不適合長期存在**
    
----------

### 路徑 B：bbappend + patch（正式修改）

適用：

-   要進版控  
-   要交付到正式部署環境
    
關鍵點：

-   patch 必須可重現
-   recipe 不應 hardcode path
    
----------

### 路徑 C：external src（大型專案）

適用：

-   kernel / u-boot 大改
-   長期分支維護
    
風險：
-   image 可重現性下降
   
----------

## 4. Debug 決策流程
```
行為不符預期
  ├─ 確認 image 是否 rebuild
  ├─ 確認 recipe 是否被選中
  ├─ 確認 layer priority
  ├─ 確認 FILE / SRCREV
  └─ 才開始 debug code
```

**最低成本驗證法**：

```bash
strings tmp/deploy/images/*/Image | grep your_string
```
----------

## 5. Kernel / U-Boot 專用 Debug 

### 5.1 Kernel

```bash
bitbake -e virtual/kernel | grep ^SRC_URI
```

檢查：

-   patch 是否真的被套用
-   是否被其他 layer 覆蓋
    

### 5.2 U-Boot

-   常見錯誤：改了 board 但 image 沒變
    
-   驗證方式：

```bash
strings u-boot.bin | grep board_name
```

----------

## 6. 常見工程錯覺與誤判

| 現象             | 常見誤判      | 真正原因                 |
|------------------|---------------|--------------------------|
| 改 driver 沒效果 | Cache 問題    | Recipe 未重新生效        |
| 行為回到舊版     | git reset     | Layer priority 錯誤      |
| Patch 有套但無效 | Compiler bug  | SRCREV 指向錯誤          |

----------

## 8. 敘事

> 「Yocto 的問題一定先確認 layer 與 recipe 是否真的生效，  
> 在確定 image 內容正確之前，我不會直接懷疑 kernel code。」
