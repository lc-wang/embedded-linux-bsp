#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "myspin"

static DEFINE_SPINLOCK(counter_lock);
static int shared_counter;

static ssize_t my_read(struct file *file, char __user *buf,
		       size_t count, loff_t *ppos)
{
	char tmp[32];
	int len;
	int value;
	unsigned long flags;

	if (*ppos != 0)
		return 0;

	/* spin_lock_irqsave：關中斷 + 拿鎖 */
	spin_lock_irqsave(&counter_lock, flags);
	value = shared_counter;
	spin_unlock_irqrestore(&counter_lock, flags);

	len = scnprintf(tmp, sizeof(tmp), "%d\n", value);

	if (count < len)
		return -EINVAL;

	/* ⚠ 注意：copy_to_user 在鎖外 */
	if (copy_to_user(buf, tmp, len))
		return -EFAULT;

	*ppos += len;

	pr_info("myspin: read counter=%d\n", value);
	return len;
}

static ssize_t my_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	char tmp[32];
	long delta;
	int ret;
	unsigned long flags;

	if (count == 0 || count >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, buf, count))
		return -EFAULT;

	tmp[count] = '\0';

	ret = kstrtol(tmp, 10, &delta);
	if (ret)
		return ret;

	spin_lock_irqsave(&counter_lock, flags);
	shared_counter += delta;
	pr_info("myspin: update counter -> %d\n", shared_counter);
	spin_unlock_irqrestore(&counter_lock, flags);

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

static int __init my_init(void)
{
	shared_counter = 0;
	pr_info("myspin: init\n");
	return misc_register(&my_dev);
}

static void __exit my_exit(void)
{
	misc_deregister(&my_dev);
	pr_info("myspin: exit\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lcwang");
MODULE_DESCRIPTION("Minimal spinlock example");
