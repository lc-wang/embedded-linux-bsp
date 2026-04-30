
## 🧠 net_device 與 Ethernet Driver（核心結構）

本章節重點：

-   `net_device` 是什麼
-   driver 如何註冊 network device
-   `ifconfig up` / `ip link up` 背後發生什麼
-   driver lifecycle（probe → up → down）

----------

## 🧩 1. net_device 是什麼？

```
struct net_device {
    const struct net_device_ops *netdev_ops;
    char name[IFNAMSIZ];
    unsigned int flags;
    ...
};
```

👉 可以理解成：

```
"Linux kernel 對一張網卡的抽象"
```

每個 interface：

```
eth0
eth1
```

背後都有一個 `net_device`

----------

## 🔌 2. driver 如何建立 net_device

### (1) 分配

```
struct net_device *dev;

dev = alloc_etherdev(sizeof(struct priv_data));
```

👉 同時會分配：

-   net_device
-   private data（driver 自己用）

----------

### (2) 設定 ops

```
static const struct net_device_ops my_ops = {
    .ndo_open       = my_open,
    .ndo_stop       = my_stop,
    .ndo_start_xmit = my_xmit,
};

dev->netdev_ops = &my_ops;
```

----------

### (3) 註冊

```
register_netdev(dev);
```

👉 完成後：

```
ip link
```

就會看到 interface 出現

----------

## 🔁 3. Driver Lifecycle

### 🧭 整體流程

```
probe()
  ↓
alloc_etherdev()
  ↓
setup MAC / PHY
  ↓
register_netdev()
  ↓
-------------------------
user: ip link set eth0 up
-------------------------
  ↓
ndo_open()
  ↓
start DMA / IRQ
  ↓
network ready
```

----------

## 🚀 4. `ip link up` 發生什麼？

```
ip link set eth0 up
```

Kernel flow：

```
dev_open()
  ↓
__dev_open()
  ↓
netdev_ops->ndo_open()
```

👉 進入 driver：

```
static int my_open(struct net_device *dev)
{
    enable_irq();
    start_dma();
    netif_start_queue(dev);
    return 0;
}
```

----------

### 📌 重點

```
netif_start_queue(dev);
```

👉 這行代表：

```
開始允許 TX
```

----------

## 🛑 5. `ip link down`

```
ip link set eth0 down
```

Flow：

```
dev_close()
  ↓
ndo_stop()
```

driver：

```
static int my_stop(struct net_device *dev)
{
    netif_stop_queue(dev);
    disable_irq();
    stop_dma();
    return 0;
}
```

----------

## 📤 6. TX entry point

```
static netdev_tx_t my_xmit(struct sk_buff *skb,
                          struct net_device *dev)
```

Flow：

```
TCP/IP stack
  ↓
dev_queue_xmit()
  ↓
ndo_start_xmit()
```

👉 driver 要做：

```
copy skb → DMA buffer
kick hardware
```

----------

## 📥 7. RX flow（driver 端）

```
IRQ / NAPI
  ↓
napi_poll()
  ↓
build skb
  ↓
netif_receive_skb()
```

----------

## 🔧 8. private data（driver 自己的 state）

```
struct my_priv {
    void __iomem *base;
    struct napi_struct napi;
    ...
};
```

取得方式：

```
struct my_priv *priv = netdev_priv(dev);
```

----------

## 🔗 9. net_device 與 PHY 關係

```
net_device
   ↓
phydev
   ↓
PHY driver
```

通常 driver 會：

```
phy_connect()
```

或：

```
of_phy_connect()
```

----------

## 🧪 10. Debug 重點

### 查看 interface

```
ip link
```

----------

### 查看 driver

```
ethtool -i eth0
```

----------

### TX/RX

```
cat /proc/net/dev
```

----------

### queue 狀態

```
tc qdisc show dev eth0
```

----------

## 🧠 11. Checklist

### ✔ probe 階段

-   register_netdev 成功？
-   interface 有出現？

----------

### ✔ link up

-   ndo_open 有被呼叫？
-   IRQ 有 enable？
-   DMA 有啟動？

----------

### ✔ TX

-   ndo_start_xmit 有進？
-   queue 有沒有停住？

----------

### ✔ RX

-   IRQ / NAPI 有跑？
-   有沒有 skb 上來？

----------

## 🧠 12. 常見問題

### ❌ `ip link up` 沒反應

👉 檢查：

-   ndo_open 有沒有實作
-   register_netdev 是否成功

----------

### ❌ 可以 up 但不能傳

👉 檢查：

-   netif_start_queue 有沒有呼叫
-   ndo_start_xmit 是否被呼叫

----------

### ❌ 沒有 RX

👉 檢查：

-   IRQ
-   NAPI
-   DMA descriptor

----------

## 🔍 13. Trace 

可以加 log：

```
pr_info("ndo_open called\n");
pr_info("xmit called len=%u\n", skb->len);
```

或用 ftrace：

```
echo net_dev_xmit > /sys/kernel/debug/tracing/set_event
```

