#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>

/*
  * PCLK frequency 100M
  * 1 / (100M / 100 / 128) = 128us
  */

#define MAGIC_NUMBER 'k'
#define WTD_ON 		_IO(MAGIC_NUMBER, 0)
#define WTD_OFF		_IO(MAGIC_NUMBER, 1)
#define WTD_FEED	_IO(MAGIC_NUMBER, 2)
#define WTD_READ	_IO(MAGIC_NUMBER, 3)

struct WTD_BASE {
	unsigned int wtcon;
	unsigned int wtdat;
	unsigned int wtcnt;
	unsigned int wtclrint;
};

int major;
struct cdev wtd_cdev;
struct class *cls;
volatile unsigned long *mask_wtd_reset;

struct wtddev {
	struct clk *base_clk;
	volatile struct WTD_BASE *wtd_base;
};

struct wtddev wtd;

static void wtd_on(unsigned long arg)
{
	int ret;
	unsigned int buf;
	unsigned int wtcon;
	printk("%s\n", __func__);
	ret = copy_from_user(&buf, (const void __user *)arg, 4);
	if(ret < 0) {
		printk("copy_from_user return error, error NO:%d\n", ret);
	};

	wtcon = wtd.wtd_base->wtcon;
	wtcon |= (0x63 << 8) | (0x03 << 3) | (0x01 << 5);
	wtcon &= ~(0x01 << 1);
	wtcon |= (0x01 << 0);
	printk("wtcon %x\n", wtcon);
	wtd.wtd_base->wtcnt = buf;
	wtd.wtd_base->wtdat = buf;
	wtd.wtd_base->wtcon = wtcon;
}

static void wtd_off(void)
{
	printk("%s\n", __func__);
	wtd.wtd_base->wtcon &= ~(0x01 << 5);
}

static void wtd_feed(unsigned long arg)
{
	int ret;
	unsigned int buf;
	printk("%s\n", __func__);
	ret = copy_from_user(&buf, (const void __user *)arg, 4);
	if(ret < 0) {
		printk("copy_from_user return error, error NO:%d\n", ret);
	}

	wtd.wtd_base->wtcnt = buf;
}

static void wtd_read(unsigned long arg)
{
	int ret;
	unsigned int buf = wtd.wtd_base->wtcnt;
	printk("wtcnt %x\n", wtd.wtd_base->wtcnt);
	ret = copy_to_user((void __user *)arg, &buf,4);
	if(ret < 0) {
		printk("copy_to_user return error, error NO:%d\n", ret);
	};
}

static long wtd_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case WTD_ON:
			wtd_on(arg);
			break;

		case WTD_OFF:
			wtd_off();
			break;

		 case WTD_FEED:
		 	wtd_feed(arg);
			break;

		case WTD_READ:
			wtd_read(arg);
			break;

		default:
			return -EINVAL;
	}

	return 0;

}

static int wtd_open(struct inode *inode, struct file *filep)
{
	printk("wtd_open\n");
	return 0;
}

static int wtd_release(struct inode *inode, struct file *filep)
{
	printk("wtd_exit");
	return 0;
}

static struct file_operations wtd_fops = {
	.owner	= THIS_MODULE,
	.open	= wtd_open,
	.release	= wtd_release,
	.unlocked_ioctl = wtd_ioctl,
};

static int wtd_probe(struct platform_device *pdev)
{
	dev_t devid;
	struct device *dev = &pdev->dev;
	struct resource *res = NULL;
	struct resource *res1 = NULL;

	int ret;
	printk("enter %s\n", __func__);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	res1 = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if(res == NULL || res1 == NULL) {
		printk("platform_get_resource return error\n");
		printk("res = %s, res1 = %s\n", ((res == NULL) ? "NULL" : "NOT NULL"), ((res1 == NULL) ? "NULL" : "NOT NULL"));
		return -EINVAL;
	}

	printk("res: %x\n", (unsigned int)res->start);
	printk("res1: %x\n", (unsigned int)res1->start);
	wtd.base_clk = devm_clk_get(&pdev->dev, "watchdog");

	if(IS_ERR(wtd.base_clk)) {
		dev_err(dev, "failed to get timer base clk\n");
		return PTR_ERR(wtd.base_clk);
	}

	ret = clk_prepare_enable(wtd.base_clk);
	if(ret != 0) {
		dev_err(dev, "failed to enable base clock\n");
		return ret;
	}

	wtd.wtd_base = devm_ioremap_resource(&pdev->dev, res);
	mask_wtd_reset = ioremap(res1->start, 0x04);
	writel(0x00, mask_wtd_reset);

	if(wtd.wtd_base == NULL) {
		printk("devm_ioremap_resource return error");
		goto err_clk;
	}

	if(alloc_chrdev_region(&devid, 0, 1, "wdt") < 0) {
		printk("%s ERROR\n", __func__);
		goto err_clk;
	}

	major = MAJOR(devid);
	cdev_init(&wtd_cdev, &wtd_fops);
	cdev_add(&wtd_cdev, devid, 1);
	cls  = class_create(THIS_MODULE, "mywdt");
	device_create(cls, NULL, MKDEV(major, 0), NULL, "wdt");
	return 0;

err_clk:
	clk_disable(wtd.base_clk);
	clk_unprepare(wtd.base_clk);
	return -EINVAL;
	
}

static int wtd_remove(struct platform_device *pdev)
{
	printk("enter %s\n", __func__);
	device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);
	cdev_del(&wtd_cdev);
	unregister_chrdev_region(MKDEV(major, 0), 1);
	clk_disable(wtd.base_clk);
	clk_unprepare(wtd.base_clk);
	iounmap(mask_wtd_reset);
	printk("%s leave.\n",__func__);
	return 0;
}

static const struct of_device_id wtd_dt_ids[] = {
	{.compatible = "tiny4412,wdt_demo"},
	{},
		
};
MODULE_DEVICE_TABLE(of, wtd_dt_ids);

static struct platform_driver wtd_driver= {
	.driver = {
		.name = "wdt_demo",
		.of_match_table = of_match_ptr(wtd_dt_ids),
	},
	.probe = wtd_probe,
	.remove = wtd_remove,
};

static int wtd_init(void)
{
	int ret;
	printk("enter %s\n", __func__);

	ret=platform_driver_register(&wtd_driver);
	if(ret ) {
		printk(KERN_ERR "wtd_demo: probe failed : %d\n", ret);
	}

	return ret;
}

static void wtd_exit(void)
{
	printk("enter %s\n", __func__);
	platform_driver_unregister(&wtd_driver);
}

module_init(wtd_init);
module_exit(wtd_exit);
MODULE_LICENSE("GPL");

