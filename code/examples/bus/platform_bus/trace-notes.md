# Kernel trace notes — platform_bus

## 1. Device Tree 是什麼時候變成 device 的？

在 kernel boot 時：
```text
start_kernel()
└─ setup_arch()
└─ unflatten_device_tree()
└─ of_platform_populate()
```

## 2. DTS → platform_device
```text
of_platform_populate()
└─ of_platform_device_create()
└─ platform_device_register()
└─ device_add()
```

此時：

- struct device 已存在
- 但 driver 尚未匹配

## 3. driver 註冊流程
```text
platform_driver_register()
└─ driver_register()
└─ bus_add_driver()
└─ bus_for_each_dev()
└─ platform_bus.match()
```

## 4. match() 做什麼？
```text
platform_bus.match()
```

比對順序：

1. of_match_table（compatible）
2. platform_device_id
3. name

## 5. probe() 什麼時候會被呼叫？

只有在：
```text
platform_device 已存在
AND
platform_driver 註冊完成
AND
match() 成功
```

才會呼叫：
```text
driver.probe()
```

## 6. 關鍵心智模型
```text
DTS
↓
platform_device
↓
platform bus
↓
match
↓
probe()
```

## 7. 常見問題與排查（常見誤解）

✗ DTS 直接呼叫 probe  
✗ module_init() = probe  

✓ 正確是：
```text
module_init() → driver_register()
probe() → device + driver matched
```
