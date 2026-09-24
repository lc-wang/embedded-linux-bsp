# Linux Networking Stack / Netdevice / NAPI 完整解析

本章介紹 Linux 網路堆疊架構、各層模組與 Kernel 內部的資料流，包含：

- Socket → TCP/IP → Netfilter → Qdisc → Netdevice
- NAPI / interrupt mitigation
- 驅動如何收/送封包（RX/TX path）
- Packet buffer（sk_buff）
- Routing / Neighbor / ARP
- 常見 Debug 工具與網路問題排查

適用於 Ethernet / Wi-Fi / 虛擬網卡 / 驅動開發。

## 1. Linux Network Stack 總覽

網路資料流（TX path）：
```text
User space socket
↓
Kernel socket layer
↓
TCP/UDP
↓
IP layer
↓
Routing lookup
↓
Netfilter (iptables)
↓
Qdisc (traffic control)
↓
Netdevice driver
↓
DMA → Hardware TX
```

RX path 則反向：
```text
Hardware RX interrupt
↓
NAPI poll
↓
Netdevice → netif_receive_skb()
↓
Netfilter
↓
IP → TCP/UDP
↓
Socket receive buffer
↓
User space recv()
```

## 2. sk_buff（核心資料結構）

`struct sk_buff` 是 Linux 網路的 *靈魂*。

包含：

- MAC / IP / TCP header pointers
- data pointer
- buffer length
- checksum flag
- socket pointer
- queue linkage

示意圖：
```text
+-----------------------+
| sk_buff metadata |
+-----------------------+
| headroom |
+-----------------------+
| protocol headers |
+-----------------------+
| payload |
+-----------------------+
| tailroom |
+-----------------------+
```

常見 API：

```c
netif_rx()
netif_receive_skb()
dev_queue_xmit()
skb_put()
skb_pull()
skb_push()
```

## 3. Netdevice（網卡抽象層）

每個網路介面，例如：

-   eth0    
-   wlan0   
-   lo 
-   usb0

都由 `struct net_device` 表示。

必填欄位（驅動必實作）：

```c
ndo_open
ndo_stop
ndo_start_xmit
ndo_set_mac_address
ndo_set_rx_mode
ndo_set_features
```
特別重要：
```c
ndo_start_xmit()
```
TX 的入口點：

```text
SKB → ndo_start_xmit → Driver → DMA → HW
```

## 4. NAPI（New API）— 解決 RX interrupt storm 的機制

舊模式：**每收到一個封包就 interrupt → CPU overload**

NAPI 方式：

```text
1. RX interrupt 到來
2. Driver 禁掉 interrupt，進入 poll mode
3. poll() 一次處理多個封包（batch）
4. queue 清空後，重新 enable interrupt
```
NAPI API：
```c
netif_napi_add(dev, &napi, poll_fn, weight);
napi_schedule();
napi_complete_done();
```
NAPI 是 Wi-Fi / Ethernet 高效能的關鍵。

## 5. TX path（傳送流程）

```text
socket send()
  ↓
tcp_sendmsg
  ↓
ip_queue_xmit
  ↓
dev_queue_xmit
  ↓
qdisc_run
  ↓
ndo_start_xmit (driver)
  ↓
DMA → NIC TX queue
  ↓
NIC 發送 frame
```

TX driver 要：

-   填 TX descriptor    
-   map DMA
-   提醒 NIC 發送

### 5.1 若 TX queue 滿了

```c
netif_stop_queue(dev);
...
netif_wake_queue(dev);
```

## 6. RX path（接收流程）

```text
NIC RX DMA
  ↓
RX interrupt → NAPI poll
  ↓
driver 複製資料 → skb
  ↓
netif_receive_skb()
  ↓
Netfilter
  ↓
IP / TCP
  ↓
socket receive buffer
  ↓
recv()
```

驅動需：

-   重新分配 RX buffer 
-   refill RX ring   
-   維持 DMA 正確性（cache 一致性）

## 7. Qdisc（排程器）

Linux 的 traffic shaper。

預設：

```c
pfifo_fast 或 fq_codel
```
查看：

```c
tc qdisc show dev eth0
```

高階使用：

-   shaping (HTB, TBF)    
-   QoS    
-   rate limiting

## 8. Netfilter（iptables）

流程：

```text
PREROUTING
INPUT
FORWARD
OUTPUT
POSTROUTING
```
用途：

-   NAT
-   防火牆
-   包裝修改（mangle）

## 9. Routing / Neighbor

Routing table
```c
ip route show
```
ARP table
```c
ip neigh
```
若 ARP fail → 傳送不到對方。

## 10. Wi-Fi 與 Ethernet 的差異

| 項目 | Ethernet | Wi-Fi |
|------|----------|--------|
| 介面層 | L2 frame | 802.11 frame（更複雜） |
| 速率控制 | 固定 | 動態（rate control） |
| 管理框架 | 無 | auth / assoc / beacon |
| cfg80211 / mac80211 | 不使用 | Wi-Fi driver 使用 |
| Queue 管理 | 單一 queue | 多 queue（WME / QoS） |

你的 wifi-stack.md 可與此互相連結。

## 11. 常見 Debug 工具

查看封包
```sh
tcpdump -i eth0
tcpdump -i wlan0 -vvv
```
驅動層 debug
```sh
ethtool -S eth0
```

查看：

-   RX dropped
-   TX timeout   
-   ring buffer overflow

查看 CPU 使用
```sh
perf top
```
若 RX path 過重 → NAPI 配置可能不佳。

查看 socket 狀態
```sh
ss -ant
```

## 12. 常見問題與排查

| 問題 | 可能原因 | 解法 |
|------|-----------|-------|
| RX drop 增加 | NAPI weight 太小 or RX ring 太小 | 增加 NAPI weight / RX desc |
| TX timeout | DMA 錯誤 or NIC 停住 | reset NIC / 啟用 watchdog |
| Throughput 很低 | checksum offload 未啟用 | ethtool 開啟 tx/rx offload |
| jitter 大 | qdisc 不佳 | 使用 fq_codel |
| ARP fail | L2 frame 未送出 | 檢查 MAC filter / promisc |
| Wi-Fi 慢 | rate control / power save 問題 | 停用 Wi-Fi powersave |
