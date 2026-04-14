#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>

#define DEVICE_NAME "myfault"

static struct page *fault_page;

/* ================= fault handler ================= */

static vm_fault_t my_fault(struct vm_fault *vmf)
{
	struct page *page = fault_page;

	pr_info("myfault: page fault triggered\n");

	if (!page)
		return VM_FAULT_SIGBUS;

	get_page(page);
	vmf->page = page;

	return 0;
}

static const struct vm_operations_struct my_vm_ops = {
	.fault = my_fault,
};

/* ================= mmap ================= */

static int my_mmap(struct file *file, struct vm_area_struct *vma)
{
	pr_info("myfault: mmap called\n");

	vma->vm_ops = &my_vm_ops;

	return 0;
}

/* ================= file ops ================= */

static const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.mmap  = my_mmap,
};

static struct miscdevice my_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DEVICE_NAME,
	.fops  = &my_fops,
};

/* ================= module init ================= */

static int __init my_init(void)
{
	fault_page = alloc_page(GFP_KERNEL);
	if (!fault_page)
		return -ENOMEM;

	pr_info("myfault: init\n");

	return misc_register(&my_dev);
}

static void __exit my_exit(void)
{
	misc_deregister(&my_dev);

	if (fault_page)
		__free_page(fault_page);

	pr_info("myfault: exit\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lcwang");
MODULE_DESCRIPTION("Page fault mmap example");
