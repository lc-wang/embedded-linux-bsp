# Rockchip Boot Logo 問題分析與修正報告

## 1. 問題概述（問題背景）

在不同 SoC 平台上，開機 Logo 的顯示機制並不一致。

-   **NXP (i.MX 系列)** 採用 Linux **framebuffer（fbcon）** 機制顯示內建的 Tux 企鵝圖。
    
-   **Rockchip (RK 系列)** 則採用 **DRM 子系統 (Direct Rendering Manager)** 的自家早期顯示邏輯，  
    在 kernel 啟動初期讀取 BMP 檔顯示 Logo。
    
目標：  
希望讓 Rockchip 平台的開機 Logo 也能顯示 Linux 企鵝圖，  
達到與 NXP 平台一致的外觀與行為。

## 2. 分析過程

### 2.1 NXP 與 Rockchip 顯示機制差異

| 項目 | NXP (Framebuffer) | Rockchip (DRM) |
|------|--------------------|----------------|
| **核心模組** | `drivers/video/logo/` | `drivers/gpu/drm/rockchip/rockchip_drm_logo.c` |
| **檔案格式** | `.ppm`（調色盤影像） | `.bmp`（24/32-bit） |
| **控制方式** | `Kconfig：CONFIG_LOGO_*` | `Device Tree：logo,kernel`, `logo,uboot` |
| **顯示階段** | Framebuffer 初始化時 | DRM pipeline 啟動時 |
| **顯示來源** | 內建至 kernel 映像 | `/boot/logo_kernel.bmp` 由 firmware loader 讀取 |
| **架構目的** | 傳統 console splash | DRM-based early splash / boot logo |

### 2.2 原始現象與分析流程

| 步驟 | 驗證項目 | 結果 |
|------|-----------|------|
| 1 | 檢查 DTS 是否含 `logo,kernel` 屬性 | 發現存在，如 `logo,kernel = "logo_kernel.bmp"` |
| 2 | 驗證 `CONFIG_LOGO` 設定 | 為 `n`，因 Rockchip BSP 預設關閉 framebuffer |
| 3 | 檢查顯示流程 | 確定走 DRM early logo (`rockchip_drm_logo.c`)，非 framebuffer |
| 4 | 確認實際讀取行為 | kernel 透過 firmware API 從 `/boot` 載入 BMP 檔 |
| 5 | 目標明確化 | 想以相同 DRM 架構顯示 Linux 企鵝圖 |

### 2.3 DRM Logo 顯示架構說明

Rockchip 自定義的 early boot logo 顯示路徑如下：

1.  DRM core 啟動時，呼叫 `rockchip_drm_logo_init()`。
    
2.  解析 Device Tree 節點，例如：
  ```dts
route_hdmi1: route-hdmi1 {
    logo,uboot = "logo.bmp";
    logo,kernel = "logo_kernel.bmp";
    logo,mode = "center";
    connect = <&vp1_out_hdmi1>;
};
 ```
 
3.  驅動載入 BMP 檔案並建立 framebuffer plane：
    
    -   搜尋路徑：`/boot/`, `/lib/firmware/`, `/usr/lib/firmware/`
        
    -   呼叫 `rockchip_show_bmp_logo()`
        
4.  在 framebuffer plane 上顯示 logo（覆蓋後續 console 輸出）。
    
> 此流程完全繞過 `drivers/video/logo`，  
> 因此 `CONFIG_LOGO_ROCKCHIP_CLUT224`、`CONFIG_LOGO_LINUX_CLUT224` 等設定皆不影響顯示。

## 3. Root Cause 分析（問題本質）

-   **Framebuffer 機制** 的 `logo_linux_clut224.ppm` 無法在 Rockchip 顯示，因未啟用 `CONFIG_FB`。
    
-   **Rockchip DRM** 只接受 **BMP 檔案**，不支援 `.ppm`。
    
-   若想顯示企鵝圖，需 **將 Linux 內建 logo 轉換成 BMP**，再由 DRM 顯示。
    
## 4. 解決方案

### 4.1 解法評估

| 方案 | 說明 | 優點 | 缺點 |
|------|------|------|------|
| **A. 啟用 framebuffer logo** | 修改 kernel config 啟用 `CONFIG_FB` + `CONFIG_LOGO_LINUX_CLUT224` | 顯示 Linux 企鵝 | 與 DRM 流程重疊，架構不建議 |
| **B. 停用 DRM logo (DTS)** | 移除 `logo,kernel`、`logo,uboot` | fallback 至 console 顯示 | 無法早期顯示 splash |
| ✓ **C. 保留 DRM 架構，轉換 PPM 為 BMP** | 將 Linux logo 轉成 BMP，直接由 Rockchip DRM 顯示 | 架構簡潔、行為一致 | 需一次性轉檔 |

### 4.2 採用方案：轉換 Linux Logo 為 BMP

#### 4.2.1 轉換步驟

從 kernel 原始檔取得 Linux 企鵝圖：
```bash
cp drivers/video/logo/logo_linux_clut224.ppm .
sudo apt install imagemagick
convert logo_linux_clut224.ppm logo_kernel.bmp
```

可選參數：
```bash
convert logo_linux_clut224.ppm -background white -alpha remove -alpha off BMP3:logo_kernel.bmp
```

#### 4.2.2 檔案放置

將產生的檔案放入：

`/boot/logo_kernel.bmp` 

#### 4.2.3 Device Tree 修改

```dts
route_hdmi1: route-hdmi1 {
    logo,kernel = "logo_kernel.bmp";
    logo,uboot = "logo_kernel.bmp";
    logo,mode = "center";
    connect = <&vp1_out_hdmi1>;
};
```

#### 4.2.4 驗證開機結果

`dmesg` 輸出應包含：

`rockchip_logo: Show logo_kernel.bmp success` 

螢幕顯示 Linux Tux 圖。

### 4.3 進階應用

可於多輸出裝置啟用相同設定：
```dts
route_dp1: route-dp1 {
    logo,kernel = "logo_kernel.bmp";
    connect = <&vp1_out_dp1>;
};
```

### 4.4 驗證與結果

| 測試項目 | 結果 |
|-----------|------|
| DTS 無自家 logo 設定 | ✓ 已移除 Rockchip 預設 logo 檔 |
| DRM 載入 BMP 成功 | ✓ 顯示企鵝圖 |
| Kernel 無 framebuffer 依賴 | ✓ 維持原 DRM 架構 |
| 整體架構相容性 | ✓ 可與 U-Boot logo 串接顯示 |

## 5. 結論與建議

### 5.1 結論

> Rockchip 平台的開機 Logo 顯示完全基於 **DRM early logo 機制**，  
> 而非傳統的 framebuffer。
> 
> 若想顯示 Linux 企鵝圖，**不需修改 kernel 架構**，  
> 只需將原本的 PPM 檔轉成 BMP 並在 Device Tree 指定，即可讓 DRM 正常載入。
> 
> 這是 **最簡潔、最穩定、完全不違背現有 Rockchip 架構** 的方法。

### 5.2 最終建議

| 使用場景 | 建議做法 |
|-----------|-----------|
| 只想換成 Linux logo | ✓ 採方案 C（轉檔 BMP） |
| 想統一所有平台架構 | 可考慮 Framebuffer 機制，但需重啟 console pipeline |
| 臨時測試 | DTS 可直接指定其他 BMP 測試檔 |

## 附錄

### A. 相關設定摘要

```bash
# defconfig 若要啟用 framebuffer logo（僅測試用途）
CONFIG_FB=y
CONFIG_FB_SIMPLE=y
CONFIG_LOGO=y
CONFIG_LOGO_LINUX_CLUT224=y
# CONFIG_LOGO_ROCKCHIP_CLUT224 is not set
```

```dts
# route 節點示例
route_hdmi1: route-hdmi1 {
    logo,kernel = "logo_kernel.bmp";
    logo,mode = "center";
    connect = <&vp1_out_hdmi1>;
};
```
