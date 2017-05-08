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

#define LED_CNT 4

static int major;
static struct cdev led_cdev;
static struct class *cls;
static int led1, led2, led3, led4;

static ssize_t led_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf;
	int minor = iminor(file->f_inode);

	printk("minor is %d\n", minor);
	printk("%s\n", __func__);

	if(count != 1) {
		printk("count = %d\n", count);
		return 1;
	}

	if(copy_from_user(&buf, user_buf, count)) {
		printk("copy_from_user return error\n");
		return -EFAULT;
	}

	printk("receive int buf = %d\n", buf);
	printk("receive char buf = %c\n", buf);
	if(buf == 0x01) {
		switch(minor) {
			case 0:
				gpio_set_value(led1, 0);
				break;

			case 1:
				gpio_set_value(led2, 0);
				break;

			case 2:
				gpio_set_value(led3, 0);
				break;

			case 3:
				gpio_set_value(led4, 0);
				break;

			default:
				printk("%s receive minor error\n", __func__);
				break;
		}
	} else if (buf == 0x00) {
		switch(minor) {
			case 0:
				gpio_set_value(led1, 1);
				break;

			case 1:
				gpio_set_value(led2, 1);
				break;

			case 2:
				gpio_set_value(led3, 1);
				break;

			case 3:
				gpio_set_value(led4, 1);
				break;

			default:
				printk("%s receive minor error\n", __func__);
				break;
		}
	}

	return count;
}

static int led_open(struct inode *inode, struct file *file)
{
	printk("led_open \n");
	return 0;
}

static ssize_t led_read (struct file *fd, char __user * buf, size_t count, loff_t *ppos)
{
	int minor = iminor(fd->f_inode);
	char gpio_value = 0;

	printk("minor is %d\n", minor);
	printk("%s\n", __func__);

	switch(minor) {
		case 0:
			gpio_value =  gpio_get_value(led1);
			break;

		case 1:
			gpio_value = gpio_get_value(led2);
			break;

		case 2:
			gpio_value = gpio_get_value(led3);
			break;

		case 3:
			gpio_value = gpio_get_value(led4);
			break;
	}

	printk("gpio_get_value return %d\n", gpio_value);

	if(copy_to_user(buf, &gpio_value, 1)) {
		printk("copy_to_user return error\n");
		return -EFAULT;
	}

	return 1;
}

static struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.write = led_write,
	.read = led_read,
};

static int led_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	dev_t devid;
	struct pinctrl *pctrl;
	struct pinctrl_state *pstate;
	pctrl = devm_pinctrl_get(dev);
	if(pctrl == NULL) {
		printk("devm_pinctrl_get return error");
	}

	pstate = pinctrl_lookup_state(pctrl, "led_demo");
	if(pstate == NULL) {
		printk("pinctrl_lookup_state retrun error");
	}

	pinctrl_select_state(pctrl, pstate);  // set gpio to output mode
	printk("enter %s! \n", __func__);
	led1 = of_get_named_gpio(dev->of_node, "tiny4412,int_gpio1", 0);
	led2 = of_get_named_gpio(dev->of_node, "tiny4412,int_gpio2", 0);
	led3 = of_get_named_gpio(dev->of_node, "tiny4412,int_gpio3", 0);
	led4 = of_get_named_gpio(dev->of_node, "tiny4412,int_gpio4", 0);
	if(led1 <= 0) {
		printk("led1 of_get_named_gpio return error\n");
		return -EINVAL;
	} else {
		printk("led1 %d\n", led1);
		printk("led2 %d\n", led2);
		printk("led3 %d\n", led3);
		printk("led4 %d\n", led4);
		devm_gpio_request_one(dev, led1, GPIOF_OUT_INIT_HIGH, "LED1");
		devm_gpio_request_one(dev, led2, GPIOF_OUT_INIT_HIGH, "LED2");
		devm_gpio_request_one(dev, led3, GPIOF_OUT_INIT_HIGH, "LED3");
		devm_gpio_request_one(dev, led4, GPIOF_OUT_INIT_HIGH, "LED4");
	}

	if(alloc_chrdev_region(&devid, 0, LED_CNT, "led") < 0) {
		printk("alloc_chrdev_region return error");
		goto error;
	}

	major = MAJOR(devid);

	cdev_init(&led_cdev, &led_fops);
	cdev_add(&led_cdev, devid, LED_CNT);

	cls = class_create(THIS_MODULE, "led");
	device_create(cls, NULL, MKDEV(major, 0), NULL, "led0");
	device_create(cls, NULL, MKDEV(major ,1), NULL, "led1");
	device_create(cls, NULL, MKDEV(major ,2), NULL, "led2");
	device_create(cls, NULL, MKDEV(major ,3), NULL, "led3");

error:
	unregister_chrdev_region(MKDEV(major, 0), LED_CNT);
	
	return 0;
}

static int led_remove(struct platform_device *pdev)
{
	printk("enter %s\n", __func__);
	device_destroy(cls, MKDEV(major, 0));
	device_destroy(cls, MKDEV(major, 1));
	device_destroy(cls, MKDEV(major, 2));
	device_destroy(cls, MKDEV(major, 3));
	class_destroy(cls);

	cdev_del(&led_cdev);
	unregister_chrdev_region(MKDEV(major, 0), LED_CNT);

	printk("leave %s\n", __func__);
	return 0;
}

static const struct of_device_id led_dt_ids[] ={
	{.compatible = "tiny4412,led_demo"},
	{},
};

MODULE_DEVICE_TABLE(0f, led_dt_ids);

static struct platform_driver led_driver = {
	.driver = {
		.name = "led_demo",
		.of_match_table = of_match_ptr(led_dt_ids),
	},
	.probe = led_probe,
	.remove = led_remove,
};

static int led_init(void) {
	int ret;
	printk("enter %s\n", __func__);
	ret = platform_driver_register(&led_driver);
	if(ret) 
		printk(KERN_ERR "led_demo: probe failed: %d", ret);

	return ret;
}

static void led_exit(void)
{
	printk("enter %s\n", __func__);
	platform_driver_unregister(&led_driver);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
