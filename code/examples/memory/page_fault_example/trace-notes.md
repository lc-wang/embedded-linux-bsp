
# Kernel trace notes — page_fault_example  

  
# 🟢 Level 1
  
你在 userspace 做這件事：  
```
p[0] = 'A';
```
  
但這塊 memory：  
  
👉 **其實還沒有真的存在**  
  
所以 CPU 會觸發：  
```
page fault
```
  
接著 kernel 會：  
```
1.  發現這塊 memory 還沒準備好
2.  去問 driver：「這頁要給什麼？」
3.  driver 回一個 page
4.  kernel 幫你建立 mapping
  ```
之後再存取，就不會再 fault。  
  
---  
  
# 🟡 Level 2
```
userspace 存取 memory  
↓  
page fault 發生  
↓  
kernel 找到對應的 VMA（vm_area_struct）  
↓  
呼叫 vma->vm_ops->fault()  
↓  
driver 提供一個 page  
↓  
mapping 建立完成
```
  
---  
  
## 🧠 這一章做的事  
  
在 `.mmap()`：  

vma->vm_ops = &my_vm_ops;

  
在 `.fault()`：  

alloc_page → 回傳給 kernel

  
---  
  
# 🔴 Level 3
```
do_page_fault()  
└─ handle_mm_fault()  
└─ __handle_mm_fault()  
└─ handle_pte_fault()  
└─ do_fault()  
└─ vma->vm_ops->fault()  
└─ my_fault()
```
  
---  
  
# 🔑 fault handler 在做什麼？  
  
```
get_page(page);  
vmf->page = page;
```
意思是：

告訴 kernel：  
「這一頁給你，用這個 page」

----------

# 🔥 與 mmap 的差別


### mmap vs page fault
  
| 項目 | mmap | page fault|  
|-------------|------------------|----------------------|  
| mapping | 一開始就做完 | 用到才做 |  
| page | 已經存在 | fault 時才分配 |  
| 行為 | 靜態 | 動態 |  
| 真實 driver | 少 | 非常多 |

----------

# 🧠 最重要結論

mmap ≠ 拿到記憶體  
page fault = 真正拿到 page

----------

# 🔧 Debug 建議

### 看 fault 觸發
```
pr_info("myfault: page fault triggered\n");
```
### userspace

第一次 access → 一定會看到 log  
第二次 access → 不會再觸發

----------

# 🧠 心智模型

memory mapping  
只是「承諾」  
  
page fault  
才是「兌現」
