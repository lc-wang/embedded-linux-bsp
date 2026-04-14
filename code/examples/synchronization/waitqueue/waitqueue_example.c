#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "mywait"

static DECLARE_WAIT_QUEUE_HEAD(my_wq);

static char buffer[128];
static int data_available;

/* ================= read ================= */

static ssize_t my_read(struct file *file, char __user *buf,
		       size_t count, loff_t *ppos)
{
	int len;

	pr_info("mywait: read called, waiting for data...\n");

	/* 等待條件成立 */
	wait_event(my_wq, data_available != 0);

	pr_info("mywait: data available\n");

	len = min(count, (size_t)data_available);

	if (copy_to_user(buf, buffer, len))
		return -EFAULT;

	data_available = 0;

	return len;
}

/* ================= write ================= */

static ssize_t my_write(struct file *file,
			const char __user *buf,
			size_t count,
			loff_t *ppos)
{
	size_t len = min(count, sizeof(buffer));

	if (copy_from_user(buffer, buf, len))
		return -EFAULT;

	data_available = len;

	pr_info("mywait: data written, waking up readers\n");

	/* 喚醒等待的 thread */
	wake_up(&my_wq);

	return len;
}

static const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.read  = my_read,
	.write = my_write,
};

static struct miscdevice my_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DEVICE_NAME,
	.fops  = &my_fops,
};

static int __init my_init(void)
{
	data_available = 0;
	pr_info("mywait: init\n");
	return misc_register(&my_dev);
}

static void __exit my_exit(void)
{
	misc_deregister(&my_dev);
	pr_info("mywait: exit\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lcwang");
MODULE_DESCRIPTION("Waitqueue example");
