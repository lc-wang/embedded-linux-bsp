
# 🧩 Kakip Board 網路異常除錯報告（static IP → DHCP network）


## 📌 問題摘要

Kakip 開發板接上 switch 後，網路介面 `end0` 無法取得 lab network 的 DHCP IP：

預期：192.0.2.X

實際：198.51.100.10

即使實體網路連線正常（RJ45、1000M link up），仍無法取得正確 IP，導致：

-   無法連 lab network
    
-   無 default route
    
-   ping gateway 失敗
    

----------

## 🖥️ 系統環境

-   Board：Kakip
    
-   OS：Ubuntu (custom / BSP image)
    
-   Network interface：`end0`（此板設計上即為 eth0）
    
-   Network manager：**NetworkManager**
    
-   ConnMan：未啟用
    
-   DHCP client：
    
    -   `dhclient` ❌
        
    -   `udhcpc` ❌
        

----------

## 🔍 問題現象

### IP 狀態
```
$ ifconfig end0

inet 198.51.100.10 netmask 255.255.255.0
```
### Routing table
```
$ ip route

198.51.100.0/24 dev end0 scope link
```
-   ❌ 無 `default via`
    
-   ❌ 無 gateway
    

### ARP / Neighbor
```
$ ip neigh

(empty)
```
  
```
$ arp -n

(empty)
```
### Ping gateway
```
$ ping 198.51.100.1

Destination Host Unreachable
```
----------

## 🔌 實體網路確認
```
$ ethtool end0

Link detected: yes

Speed: 1000Mb/s

Duplex: Full
```
```
$ ip -s link show end0

RX packets: >10k

TX packets: normal
```
✅ PHY 正常 ✅ RJ45 正常 ✅ switch port 有流量

----------

## 🧠 問題分析

### 關鍵線索

使用 NetworkManager 查詢 connection profile：
```
$ nmcli connection show "有線接続 1"
```
發現：
```
ipv4.method: manual

ipv4.addresses: 198.51.100.10/24
```
----------

## ❗ 根本原因（Root Cause）

> **NetworkManager 被設定為 Static IP（manual），導致 DHCP 完全沒有啟動。**

因此出現以下連鎖問題：


| 現象                     | 原因說明                           |
|--------------------------|------------------------------------|
| 永遠是 198.51.100.10     | Static IP 設定殘留                 |
| 無 default route         | 未取得 DHCP gateway                |
| ARP 表為空               | IP 不屬於實際 L2 網段              |
| ping 失敗                | 與實際 Switch / VLAN 不相符        |
| 無法取得 192.0.2.x       | DHCP client 根本未執行             |


## 🔧 解決方式

### 1️⃣ 將 IPv4 改回 DHCP
```
sudo nmcli connection modify "有線接続 1" \

ipv4.method auto \

ipv4.addresses "" \

ipv4.gateway "" \

ipv4.dns ""
```
----------

### 2️⃣ 重新啟用連線
```
sudo nmcli connection down "有線接続 1"

sudo nmcli connection up "有線接続 1"
```
----------

### 3️⃣ 驗證結果
```
$ ip addr show end0

inet 192.0.2.XX/24
```
  
```
$ ip route

default via 192.0.2.1 dev end0
```
✅ 成功取得 DHCP IP

----------

## ✅ 最終狀態

-   end0 正常由 NetworkManager 管理
    
-   DHCP DISCOVER / OFFER 正常
    
-   IP 正確取得 `192.0.2.X`
    
-   Gateway、ARP、routing table 全部正常
    

----------

## 🧩 問題總結

### ❌ 不是以下問題：

-   ❌ 非 switch 問題
    
-   ❌ 非 PHY / driver 問題
    
-   ❌ 非 cable 問題
    
-   ❌ 非 VLAN 錯誤
    
-   ❌ 非 DHCP server 故障
    

### ✅ 真正原因：

> **NetworkManager connection profile 被設定為 static IP（manual）。**
