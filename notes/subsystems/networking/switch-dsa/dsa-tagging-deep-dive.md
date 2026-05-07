
## 🧠 DSA Tagging Deep Dive

本章節重點：

-   DSA tagging 在封包裡長什麼樣
-   skb 在 TX / RX 怎麼被修改
-   CPU port 為什麼需要 tagging
-   如何用 tcpdump / Wireshark debug

----------

# 🧩 1. 為什麼需要 tagging？

## 問題本質

```
CPU 只有一條線（eth0）但 switch 有多個 port
```

👉 那怎麼知道：

```
封包要去哪個 port？或從哪個 port 來？
```

## ✔ 解法

```
在封包裡加 metadata（tag）
```

# 🔌 2. DSA 封包長什麼樣？

## 原始 Ethernet frame

```
| DST MAC | SRC MAC | EtherType | Payload |
```

----------

## 加上 DSA tag

```
| DST | SRC | DSA TAG | EtherType | Payload |
```

----------

👉 TAG 內容：

```
port id
VLAN info
control bits
```

# 🔥 3. 不同 vendor 的 tag

## 🔹 Marvell

```
在 MAC header 後面插入 4 bytes
```

----------

## 🔹 Realtek

```
不同格式（可能更長）
```

----------

## 🔹 Broadcom

```
自定義 tag header
```

----------

👉 重點：

```
每個 driver 都要解析自己的 tag
```

----------

# 🔁 4. TX path

## Flow

```
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

## 🔧 關鍵操作

```
skb_push(skb, tag_len);
memcpy(... tag ...);
```

----------

👉 代表：

```
在 packet 前面插入 tag
```

----------

# 🔁 5. RX path


## Flow

```
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

----------

## 🔧 關鍵操作

```
port = parse_tag(skb);
skb_pull(skb, tag_len);
```

----------

👉 代表：

```
移除 tag + 決定來源 port
```

----------

# 🧠 6. skb memory layout

## TX 前

```
[MAC][IP][TCP]
```

----------

## TX 後

```
[MAC][DSA TAG][IP][TCP]
```

----------
## RX 前（wire）

```
[MAC][DSA TAG][IP][TCP]
```

----------

## RX 後（給 lan1）

```
[MAC][IP][TCP]
```

----------

# 🔍 7. tcpdump / Wireshark

## ✔ 抓 CPU port

```
tcpdump -i eth0 -xx
```

----------

👉 會看到：

```
奇怪的 bytes（DSA tag）
```

----------

## ✔ 抓 lan1

```
tcpdump -i lan1
```

----------

👉 不會看到 tag（已被剝掉）


# ⚠️ 8. 為什麼 tcpdump 看不到 tag？

```
因為 DSA 在 RX 時已經 remove tag
```

----------

👉 除非：

```
抓 eth0（CPU port）
```

----------

# ❗ 9. 錯誤

## 🚨 Case 1：封包收不到

👉 原因：

```
tag parsing 錯
```

----------

## 🚨 Case 2：封包送錯 port

👉 原因：

```
tag encode 錯
```

----------


## 🚨 Case 3：封包消失

👉 原因：

```
switch 不認 tag
```

----------


## 🚨 Case 4：只有 CPU 收到

👉 原因：

```
DSA demux 錯
```

----------

# 🔧 10. Debug

## ✔ 比對 eth0 vs lan1

```
tcpdump -i eth0
tcpdump -i lan1
```

----------

👉 如果：

```
eth0 有，lan1 沒
```

👉 問題在：

```
tagging / DSA core
```

----------


## ✔ 看 raw packet

```
tcpdump -i eth0 -xx
```

----------

👉 分析：

```
tag header
```

----------


## ✔ driver log

```
pr_info("TX port=%d\n", port);
```

----------


# 🧠 11. Debug Flow

```
封包問題？→ eth0 有嗎？→ tag 正確嗎？→ lanX 有嗎？
```

----------

# 🔥 12. 觀念

## ✔ Rule 1

```
DSA = 靠 packet tag 分辨 port
```

----------

## ✔ Rule 2

```
沒有 tag = switch 不知道怎麼轉
```

----------

## ✔ Rule 3

```
tag 錯 = 全部壞
```
