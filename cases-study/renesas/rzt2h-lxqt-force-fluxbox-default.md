
# 📘 LXQt 預設使用 Fluxbox 視窗管理員設定報告

## 1. 問題背景

在自製 Ubuntu rootfs image（RZ/T2H 平台）中：

-   桌面環境使用 **LXQt**
    
-   視窗管理員使用 **fluxbox**
    
-   Display manager 使用 **SLiM（autologin）**
    

但在 **第一次開機登入時**，畫面會出現：

`Please select  a window manager` 

可選項目例如：

-   Fluxbox
    
-   Xfwm4
    
-   Openbox
    
-   Others
    

此選擇視窗在 production image 與部署情境中不可接受，必須：

> 開機即自動進入 **LXQt + fluxbox**  
> 不顯示任何 session chooser。
## 2. 問題現象

在第一次登入 LXQt 時，系統顯示 window manager chooser。  
若未設定預設 WM，LXQt 會要求使用者手動選擇一次。

----------

## 3. 最終解法（已驗證可行）

在 image 製作流程（rootfs script）中，**直接建立系統層級的 LXQt session 設定檔**：
```
sync

mkdir -p /etc/xdg/lxqt

cat <<EOF > /etc/xdg/lxqt/session.conf
[General]
window_manager=fluxbox
EOF
```
### 效果

-   ✅ 第一次開機不再跳出 WM chooser
    
-   ✅ LXQt 直接使用 fluxbox
    
-   ✅ 對所有使用者生效（system default）
    

----------

## 4. 為什麼這個解法有效

LXQt session 在啟動時會讀取系統設定：

-   `/etc/xdg/lxqt/session.conf`
    

若其中包含：

`[General]  window_manager=fluxbox` 

LXQt 就能直接決定 WM，不會進入「請使用者選擇」的流程。

----------

## 5. 實作注意事項（避免復發）

### 5.1 寫入時機

此檔案需要在「第一次啟動 LXQt session 之前」就存在。  
因此建議放在：

-   `apt install lxqt / fluxbox` 完成之後
    
-   `sync` 後再寫入（確保落盤）
    

### 5.2 不要刪除 fluxbox 的 session 檔（建議）

為了避免系統缺少必要的 desktop/session 定義，建議**不要刪除**：

-   `/usr/share/xsessions/fluxbox.desktop`
    

（除非你確定 system 流程完全不依賴它）

----------

## 6. 驗證方式

開機登入後檢查：

`cat /etc/xdg/lxqt/session.conf` 

應該包含：

`[General]  window_manager=fluxbox` 

並確認登入時不再出現選擇視窗。

----------

## 7. 結論

透過在 image 端建立：

-   `/etc/xdg/lxqt/session.conf`
    

並指定：

-   `window_manager=fluxbox`
    

即可確保 LXQt 在第一次開機登入時直接使用 fluxbox，不需使用者手動選擇。
