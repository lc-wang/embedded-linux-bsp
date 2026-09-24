# DSA Tagging Deep Dive

本章節重點：

-   DSA tagging 在封包裡長什麼樣
-   skb 在 TX / RX 怎麼被修改
-   CPU port 為什麼需要 tagging
-   如何用 tcpdump / Wireshark debug

## 1. 為什麼需要 tagging？

### 1.1 問題本質

```text
CPU 只有一條線（eth0）但 switch 有多個 port
```

那怎麼知道：

```text
封包要去哪個 port？或從哪個 port 來？
```

### 1.2 解法

```text
在封包裡加 metadata（tag）
```

## 2. DSA 封包長什麼樣？

### 2.1 原始 Ethernet frame

```text
| DST MAC | SRC MAC | EtherType | Payload |
```

### 2.2 加上 DSA tag

```text
| DST | SRC | DSA TAG | EtherType | Payload |
```

TAG 內容：

```text
port id
VLAN info
control bits
```

## 3. 不同 vendor 的 tag

### 3.1 Marvell

```text
在 MAC header 後面插入 4 bytes
```

### 3.2 Realtek

```text
不同格式（可能更長）
```

### 3.3 Broadcom

```text
自定義 tag header
```

重點：

```text
每個 driver 都要解析自己的 tag
```

## 4. TX path

### 4.1 Flow

```text
lan1
 ↓
dsa_slave_xmit()
 ↓
修改 skb（push tag）
 ↓
skb->dev = eth0
 ↓
dev_queue_xmit()
 ↓
MAC
```

### 4.2 關鍵操作

```c
skb_push(skb, tag_len);
memcpy(... tag ...);
```

代表：

```text
在 packet 前面插入 tag
```

## 5. RX path

### 5.1 Flow

```text
eth0 收到
 ↓
driver 收 skb
 ↓
DSA core
 ↓
解析 tag
 ↓
skb->dev = lanX
 ↓
netif_receive_skb()
```

### 5.2 關鍵操作

```c
port = parse_tag(skb);
skb_pull(skb, tag_len);
```

代表：

```text
移除 tag + 決定來源 port
```

## 6. skb memory layout

### 6.1 TX 前

```text
[MAC][IP][TCP]
```

### 6.2 TX 後

```text
[MAC][DSA TAG][IP][TCP]
```

### 6.3 RX 前（wire）

```text
[MAC][DSA TAG][IP][TCP]
```

### 6.4 RX 後（給 lan1）

```text
[MAC][IP][TCP]
```

## 7. tcpdump / Wireshark

### 7.1 抓 CPU port

```bash
tcpdump -i eth0 -xx
```

會看到：

```text
奇怪的 bytes（DSA tag）
```

### 7.2 抓 lan1

```bash
tcpdump -i lan1
```

不會看到 tag（已被剝掉）

## 8. 為什麼 tcpdump 看不到 tag？

```text
因為 DSA 在 RX 時已經 remove tag
```

除非：

```text
抓 eth0（CPU port）
```

## 9. 常見問題與排查

### 9.1 Case 1：封包收不到

原因：

```text
tag parsing 錯
```

### 9.2 Case 2：封包送錯 port

原因：

```text
tag encode 錯
```

### 9.3 Case 3：封包消失

原因：

```text
switch 不認 tag
```

### 9.4 Case 4：只有 CPU 收到

原因：

```text
DSA demux 錯
```

## 10. Debug

### 10.1 比對 eth0 vs lan1

```bash
tcpdump -i eth0
tcpdump -i lan1
```

如果：

```text
eth0 有，lan1 沒
```

問題在：

```text
tagging / DSA core
```

### 10.2 看 raw packet

```bash
tcpdump -i eth0 -xx
```

分析：

```text
tag header
```

### 10.3 driver log

```c
pr_info("TX port=%d\n", port);
```

## 11. Debug Flow

```text
封包問題？→ eth0 有嗎？→ tag 正確嗎？→ lanX 有嗎？
```

## 12. 觀念

### 12.1 Rule 1

```text
DSA = 靠 packet tag 分辨 port
```

### 12.2 Rule 2

```text
沒有 tag = switch 不知道怎麼轉
```

### 12.3 Rule 3

```text
tag 錯 = 全部壞
```
