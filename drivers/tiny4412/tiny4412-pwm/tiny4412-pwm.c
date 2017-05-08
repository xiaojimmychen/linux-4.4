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
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/time.h>

/*
 * PWM clock 100M
 * 100M/250/2 = 100000
 * 1/100000 = 10us
 */

#define MAGIC_NUMBER	'k'
#define BEEP_ON 		_IO(MAGIC_NUMBER,	0)
#define BEEP_OFF	_IO(MAGIC_NUMBER,	1)
#define BEEP_FREQ	_IO(MAGIC_NUMBER,	2)

static int major;
static struct cdev pwm_cdev;
static struct class *cls;

struct TIMER_BASE {
	unsigned int TCFG0;
	unsigned int TCFG1;
	unsigned int TCON;
	unsigned int TCNTB0;
	unsigned int TCMPB0;
};

volatile static struct TIMER_BASE *timer = NULL;

#define BEEP_IN_FREQ	100000

static void beep_freq(unsigned long arg)
{
	printk("ioctl %d\n", (unsigned int)arg);
	timer->TCNTB0 = BEEP_IN_FREQ;
	timer->TCMPB0 = BEEP_IN_FREQ / arg;
	timer->TCON	    = (timer->TCON & (~(0x0f << 0) | (0x06 << 0)));
	timer->TCON     = (timer->TCON & ~(0xff)) | 0x0d;
}

static void beep_on(void)
{
	printk("beep on\n");
	timer->TCON = (timer->TCON & ~(0xff)) | 0x0d;
	printk("%x\n", timer->TCON);
}

static void beep_off(void)
{
	timer->TCON = timer->TCON & ~(0x01);
}

static long pwm_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
		case BEEP_ON:
			beep_on();
			break;

		case BEEP_OFF:
			beep_off();
			break;

		case BEEP_FREQ:
			beep_freq(arg);
			break;

		default:
			return -EINVAL;
	};
	return 0;
}

static int pwm_open(struct inode *inode, struct file *file)
{
	printk("pwm_open");
	return 0;
}

static int pwm_release(struct inode *inode, struct file *file)
{
	printk("pwm_exit\n");
	return 0;
}

static struct file_operations pwm_fops = {
	.owner = THIS_MODULE,
	.open   = pwm_open,
	.release = pwm_release,
	.unlocked_ioctl = pwm_ioctl,
};

static int pwm_probe(struct platform_device *pdev)
{
	dev_t devid;
	struct device *dev = &pdev->dev;
	struct resource *res = NULL;
	struct pinctrl *pctrl;
	struct pinctrl_state *pstate;
	struct clk *base_clk;
	int ret;
	printk("enter %s\n", __func__);

	pctrl = devm_pinctrl_get(dev);
	if(pctrl == NULL) {
		printk("devm_pinctrl_get return error\n");
		return -EINVAL;
	}

	pstate = pinctrl_lookup_state(pctrl, "pwm_pin");
	if(pstate == NULL) {
		printk("pinctrl_lookup_state return error");
		return -EINVAL;
	}

	pinctrl_select_state(pctrl, pstate);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(res == NULL) {
		printk("platform_get_resource return error");
		return -EINVAL;
	}

	printk("res : %x\n", (unsigned int)res->start);
	base_clk = devm_clk_get(&pdev->dev, "timers");
	if(IS_ERR(base_clk)) {
		dev_err(dev, "failed to get timer base clk\n");
		return PTR_ERR(base_clk);
	}

	ret = clk_prepare_enable(base_clk);
	if(ret < 0) {
		dev_err(dev, "failed to enable base clock\n");
		return ret;
	}

	timer = devm_ioremap_resource(&pdev->dev, res);
	if(timer == NULL ) {
		printk("dev_ioremap_resource return error\n");
		return -EINVAL;
	}

	printk("timer: %x\n", (unsigned int)timer);

	timer->TCFG0 = (timer->TCFG0 & ~(0xff << 0)) | (0xfa << 0);
	timer->TCFG1 = (timer->TCFG1 & ~(0x0f << 0)) | (0x02 << 0);
	timer->TCNTB0 = 100000;
	timer->TCMPB0 = 90000;
	timer->TCON = (timer->TCON & ~(0x0f << 0)) | (0x06 << 0);
	printk("%x %x %x %x %x\n", timer->TCFG0, timer->TCFG1, timer->TCNTB0, timer->TCMPB0, timer->TCON);

	if(alloc_chrdev_region(&devid, 0, 1, "pwm") < 0) {
		printk("alloc_chrdev_region return error\n");
		goto error;
	}

	major = MAJOR(devid);
	cdev_init(&pwm_cdev, &pwm_fops);
	cdev_add(&pwm_cdev, devid, 1);
	cls = class_create(THIS_MODULE, "mypwm");
	device_create(cls, NULL, MKDEV(major, 0), NULL, "pwm0");

error:
	unregister_chrdev_region(MKDEV(major, 0), 1);
	return 0;
	
}

static int pwm_remove(struct platform_device *pdev)
{
	printk("enter %s\n", __func__);
	device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);
	cdev_del(&pwm_cdev);
	unregister_chrdev_region(MKDEV(major, 0), 1);
	printk("leave %s\n", __func__);
	return 0;
}

static const struct of_device_id pwm_dt_ids[] = {
	{.compatible = "tiny4412,pwm_demo"},
	{},
};

MODULE_DEVICE_TABLE(of, pwm_dt_ids);

static struct platform_driver pwm_driver = {
	.driver = {
		.name = "pwm_demo",
		.of_match_table = of_match_ptr(pwm_dt_ids),
	},
	.probe = pwm_probe,
	.remove = pwm_remove,
};

static int pwm_init(void)
{
	int ret;
	printk("enter %s\n", __func__);
	ret = platform_driver_register(&pwm_driver);
	if(ret < 0) {
		printk(KERN_ERR "pwm_demo: probe return error, error NO: %d\n", ret);
	}
	return ret;
}

static void pwm_exit(void)
{
	printk("enter %s\n", __func__);
	platform_driver_unregister(&pwm_driver);
}

module_init(pwm_init);
module_exit(pwm_exit);

MODULE_LICENSE("GPL");