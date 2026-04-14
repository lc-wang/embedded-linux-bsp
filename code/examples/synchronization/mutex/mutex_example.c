#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "mymutex"

static DEFINE_MUTEX(counter_lock);
static int shared_counter;

static ssize_t my_read(struct file *file, char __user *buf,
		       size_t count, loff_t *ppos)
{
	char tmp[32];
	int len;
	int value;

	if (*ppos != 0)
		return 0;

	mutex_lock(&counter_lock);
	value = shared_counter;
	mutex_unlock(&counter_lock);

	len = scnprintf(tmp, sizeof(tmp), "%d\n", value);

	if (count < len)
		return -EINVAL;

	if (copy_to_user(buf, tmp, len))
		return -EFAULT;

	*ppos += len;

	pr_info("mymutex: read counter=%d\n", value);
	return len;
}

static ssize_t my_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	char tmp[32];
	long delta;
	int ret;

	if (count == 0 || count >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, buf, count))
		return -EFAULT;

	tmp[count] = '\0';

	ret = kstrtol(tmp, 10, &delta);
	if (ret)
		return ret;

	mutex_lock(&counter_lock);
	shared_counter += delta;
	pr_info("mymutex: update counter -> %d\n", shared_counter);
	mutex_unlock(&counter_lock);

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
	pr_info("mymutex: init\n");
	return misc_register(&my_dev);
}

static void __exit my_exit(void)
{
	misc_deregister(&my_dev);
	pr_info("mymutex: exit\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lcwang");
MODULE_DESCRIPTION("Minimal mutex synchronization example");
