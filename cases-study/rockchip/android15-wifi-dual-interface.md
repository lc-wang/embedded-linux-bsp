# RK3588 Android 15 – WIFI_HIDL_FEATURE_DUAL_INTERFACE 啟用問題分析報告

## 1. 問題概述

### 1.1 背景說明

在 Rockchip RK3588 Android 15 BSP 中，希望啟用 Wi-Fi 的 **STA + AP 併行（Dual Interface）模式**，  
藉由在 `BoardConfig.mk` 中設定：

```makefile
# enable dual interface
WIFI_HIDL_FEATURE_DUAL_INTERFACE := true
```
理論上應觸發 Wi-Fi HAL 編譯旗標 `DWIFI_HIDL_FEATURE_DUAL_INTERFACE`，
使` wifi_feature_flags.cpp` 生成對應的 concurrency combination（STA + AP）。

但實際測試結果：

``` bash
dumpsys wifi | grep Concurrency
STA + STA Concurrency Supported: false
STA + AP  Concurrency Supported: false
```

顯示 HAL 層並未啟用 dual interface。

### 1.2 問題現象

即使在 BoardConfig.mk 或 device.mk 中設置：

```makefile
WIFI_HIDL_FEATURE_DUAL_INTERFACE := true
```
編譯後在 Soong 輸出目錄中仍無對應旗標：

``` bash
grep -r "WIFI_HIDL_FEATURE_DUAL_INTERFACE" out/soong/.intermediates/hardware/interfaces/wifi/aidl/default/
# (no results)
```

代表 Soong 未正確傳遞該變數。

## 2. 除錯過程（分析過程）

### 2.1 檢查 Soong 定義

在 `hardware/interfaces/wifi/aidl/default/Android.bp` 可見：

```bp
soong_config_module_type {
    name: "wifi_hal_cc_defaults",
    module_type: "cc_defaults",
    config_namespace: "wifi",
    bool_variables: [
        "hidl_feature_aware",            // WIFI_HIDL_FEATURE_AWARE
        "hidl_feature_dual_interface",   // WIFI_HIDL_FEATURE_DUAL_INTERFACE
        ...
    ],
}
```
並在 `wifi_hal_cc_defaults` 的設定中包含：

```bp
soong_config_variables: {
    hidl_feature_dual_interface: {
        cppflags: ["-DWIFI_HIDL_FEATURE_DUAL_INTERFACE"],
    },
}
```

即 Soong 的實際變數名為：
``` makefile
SOONG_CONFIG_wifi_hidl_feature_dual_interface := true
```

### 2.2 BoardConfig.mk 傳遞設定

原本的設定：

``` makefile
WIFI_HIDL_FEATURE_DUAL_INTERFACE := true
```
並不會自動傳遞給 Soong。

必須在 BoardConfig.mk 或 device.mk 中明確加入：

``` makefile
SOONG_CONFIG_NAMESPACES += wifi
SOONG_CONFIG_wifi += \
    hidl_feature_dual_interface \
    hidl_feature_aware

SOONG_CONFIG_wifi_hidl_feature_dual_interface := true
SOONG_CONFIG_wifi_hidl_feature_aware := true
```

### 2.3 驗證 Soong Cache 是否更新

Android 15 不再使用 soong.variables，
改為 per-product 格式：

``` bash
out/soong/soong.rk3588.variables
out/soong/soong.rk3588.extra.variables
```
檢查是否已生成：

``` bash
grep -A5 wifi out/soong/soong.rk3588.variables
```

預期輸出：

``` json
"wifi": {
    "hidl_feature_dual_interface": true,
    "hidl_feature_aware": true
}
```

若沒有出現，代表 Soong cache 尚未更新。

### 2.4 清除舊 cache 並重建

為了讓 Soong 重新解析 config：

``` bash
rm -f out/soong/soong.rk3588.variables
rm -f out/soong/soong.rk3588.extra.variables
m android.hardware.wifi-service -j
```

重新 build Wi-Fi HAL 後，在 build log 中可看到：

``` diff
-DWIFI_HIDL_FEATURE_DUAL_INTERFACE
```

代表宏已成功帶入。

## 3. Root Cause 分析（相關輔助資訊）

若直接在 `wifi_feature_flags.cpp` 中強制：

``` cpp
#define WIFI_HIDL_FEATURE_DUAL_INTERFACE true
```

功能立即生效 → 證實問題與 Soong config 傳遞有關。

Android 15 中仍維持以下條件控制：

```cpp
#ifdef WIFI_HIDL_FEATURE_DUAL_INTERFACE
    // enable STA+AP concurrency
#endif
```

## 4. 解決方案

### 4.1 最終建議流程

在 BoardConfig.mk 中加入：

``` makefile
SOONG_CONFIG_NAMESPACES += wifi
SOONG_CONFIG_wifi += hidl_feature_dual_interface hidl_feature_aware
SOONG_CONFIG_wifi_hidl_feature_dual_interface := true
SOONG_CONFIG_wifi_hidl_feature_aware := true
```

清除 Soong Cache：

``` bash
rm -f out/soong/soong.rk3588_board*.variables
```

重建 Wi-Fi HAL：

``` bash
m android.hardware.wifi-service -j
```

驗證結果：

``` bash
dumpsys wifi | grep Concurrency
```

### 4.2 驗證結果

重新刷機後：

``` bash
dumpsys wifi | grep Concurrency
STA + STA Concurrency Supported: false
STA + AP  Concurrency Supported: true
```

Dual Interface 功能已成功啟用。

### 4.3 Debug 驗證流程

#### 4.3.1 驗證 Soong 變數生成

``` bash
grep -A5 wifi out/soong/soong.rk3588.variables
```

✓ 若出現 "hidl_feature_dual_interface": true → 設定正確

✗ 若無該段 → Soong 尚未更新 cache

#### 4.3.2 檢查 HAL 編譯旗標

``` bash
grep -r "WIFI_HIDL_FEATURE_DUAL_INTERFACE" out/soong/.intermediates/hardware/interfaces/wifi/aidl/default/
```

應看到` -DWIFI_HIDL_FEATURE_DUAL_INTERFACE` 出現在 build 命令列。

#### 4.3.3 確認 build.prop 內容

``` bash
grep wifi out/target/product/rk3588_board/system/build.prop
grep wifi out/target/product/rk3588_board/vendor/build.prop
```
若需追蹤中介檔：

``` bash
grep wifi out/target/product/rk3588_board/obj/PACKAGING/*_build.prop_intermediates/build.prop
```

#### 4.3.4 Runtime 層驗證

``` bash
adb shell getprop | grep wifi
adb shell getprop | grep vendor.wifi
adb shell getprop | grep ro.vendor.wifi
```
確認實際生效的 Wi-Fi 相關屬性。

#### 4.3.5 HAL 狀態驗證

``` bash
adb shell dumpsys wifi | grep Concurrency
```
預期顯示：

``` yaml
STA + STA Concurrency Supported: false
STA + AP  Concurrency Supported: true
```

#### 4.3.6 快速重新生成 Soong 變數

若仍未生效，可執行：

``` bash
rm -f out/soong/soong.rk3588_board*.variables
m android.hardware.wifi-service -j
```

無需全系統重建，即可讓 HAL 重吃設定。

## 5. 結論與建議

| 項目 | Android 14 | Android 15 |
|------|-------------|-------------|
| **WIFI_HIDL_FEATURE_DUAL_INTERFACE 傳遞方式** | 自動生效 | 必須透過 Soong config 顯式設定 |
| **Soong 變數名稱** | N/A | `SOONG_CONFIG_wifi_hidl_feature_dual_interface` |
| **問題來源** | 舊 cache 未更新 | ✓ 修正後正常 |
| **驗證結果** | STA+AP 無效 | STA+AP 支援成功 |

## 附錄

### A. 快速驗證指令摘要

| 目的 | 指令 | 預期結果 |
|------|-------|-----------|
| **Soong 變數檢查** | `grep -A5 wifi out/soong/soong.rk3588_board.variables` | 出現 `"hidl_feature_dual_interface": true` |
| **HAL 編譯旗標確認** | `grep -r "WIFI_HIDL_FEATURE_DUAL_INTERFACE" out/soong/.intermediates/hardware/interfaces/wifi/aidl/default/` | 有 `-D` 宏出現 |
| **build.prop 檢查** | `grep wifi out/target/product/rk3588_board/vendor/build.prop` | Wi-Fi 屬性列出 |
| **runtime 驗證** | `adb shell getprop | grep wifi` | 顯示實際生效的 Wi-Fi 相關屬性 |
| **HAL 狀態驗證** | `dumpsys wifi | grep Concurrency` | 顯示 `STA + AP  Concurrency Supported: true` |
| **快速清 cache 重建** | `rm -f out/soong/soong.rk3588_board*.variables && m android.hardware.wifi-service -j` | 重新生成 Soong 設定並重新編譯 Wi-Fi HAL |

