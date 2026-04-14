#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "mycomp"

static DECLARE_COMPLETION(my_completion);
static struct delayed_work my_work;

/* ================= work handler ================= */

static void work_func(struct work_struct *work)
{
	pr_info("mycomp: work done, calling complete()\n");

	complete(&my_completion);
}

/* ================= file ops ================= */

static ssize_t my_read(struct file *file, char __user *buf,
		       size_t count, loff_t *ppos)
{
	char msg[] = "done\n";

	pr_info("mycomp: waiting for completion...\n");

	/* 等待事件完成 */
	wait_for_completion(&my_completion);

	pr_info("mycomp: completion received\n");

	if (copy_to_user(buf, msg, sizeof(msg)))
		return -EFAULT;

	return sizeof(msg);
}

static ssize_t my_write(struct file *file,
			const char __user *buf,
			size_t count,
			loff_t *ppos)
{
	pr_info("mycomp: start work (2 sec delay)\n");

	reinit_completion(&my_completion);

	schedule_delayed_work(&my_work, msecs_to_jiffies(2000));

	return count;
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

/* ================= module init ================= */

static int __init my_init(void)
{
	INIT_DELAYED_WORK(&my_work, work_func);

	pr_info("mycomp: init\n");
	return misc_register(&my_dev);
}

static void __exit my_exit(void)
{
	cancel_delayed_work_sync(&my_work);
	misc_deregister(&my_dev);

	pr_info("mycomp: exit\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lcwang");
MODULE_DESCRIPTION("Completion example");
