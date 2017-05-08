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
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/interrupt.h>

DECLARE_WAIT_QUEUE_HEAD(wait);

static int major;
static struct cdev adc_cdev;
static struct class *cls;

struct ADC_BASE
{
	unsigned int ADCCON;
	unsigned int temp0;
	unsigned int ADCDLY;
	unsigned int ADCDAT;
	unsigned int temp1;
	unsigned int temp2;
	unsigned int CLRINTADC;
	unsigned int ADCMUX;
};

volatile static struct ADC_BASE *adc_base = NULL;

static int adc_open(struct inode *inode, struct file *file) 
{
	printk("adc_open\n");
	return 0;
}

static int adc_release(struct inode *inode, struct file *file)
{
	printk("adc_release\n");
	return 0;
}

static ssize_t adc_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
	int data = 0, ret = 0;
	printk("adc_read\n");
	adc_base->ADCMUX = 0x00;
	adc_base->ADCCON = (1 << 16 | 1 << 14 | 99 << 6 | 1 << 0);
	wait_event_interruptible(wait, ((adc_base->ADCCON >> 15) & 0x01));
	data = adc_base->ADCDAT & 0xfff;
	ret = copy_to_user(buf, &data, count);
	printk("copy_to_user return %x\n", ret);

	if(ret < 0)  {
		printk("copy_to_user return error");
		return -EINVAL;
	}

	return count;
}

static struct file_operations adc_fops = {
	.owner	= THIS_MODULE,
	.open	= adc_open,
	.read	= adc_read,
	.release 	= adc_release,
};

static irqreturn_t adc_demo_isr(int irq, void *dev_id) 
{
	printk("enter irq now to wake up\n");
	wake_up(&wait);
	adc_base->CLRINTADC = 1;
	return IRQ_HANDLED;
}

struct clk *base_clk;
int irq;
static int adc_probe(struct platform_device *pdev)
{
	dev_t devid;
	struct device *dev = &pdev->dev;
	struct resource *res = NULL;
	int ret;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(res == NULL) {
		printk("platform_get_resource return error\n");
		return -EINVAL;
	}

	base_clk = devm_clk_get(&pdev->dev, "timers");
	if(IS_ERR(base_clk)) {
		dev_err(dev, "failed to get timer base clk\n");
		return PTR_ERR(base_clk);
	}

	ret = clk_prepare_enable(base_clk);
	if(ret < 0) {
		dev_err(dev, "failed to enable base clock\n");;
		return -EINVAL;
	}

	printk("res: %x\n", (unsigned int)res->start);
	adc_base = devm_ioremap_resource(&pdev->dev,  res);
	if(adc_base == NULL) {
		printk("devm_ioremap_resource error\n");
		goto err_clk;
	}

	printk("adc_base: %x\n", (unsigned int)adc_base);
	irq = platform_get_irq(pdev, 0);
	if(irq < 0) {
		dev_err(dev, "failed to request_irq\n");
		goto err_clk;
	}

	ret = request_irq(irq, adc_demo_isr, 0, "adc", NULL);
	if(ret < 0){
		dev_err(dev, "failed to retuqest_irq\n");
		goto err_clk;
	}

	if(alloc_chrdev_region(&devid, 0, 1, "adc") < 0) {
		printk("%s ERROR\n", __func__);
		goto err_req_irq;
	}

	major = MAJOR(devid);
	cdev_init(&adc_cdev, &adc_fops);
	cdev_add(&adc_cdev, devid, 1);
	cls = class_create(THIS_MODULE, "myadc");
	device_create(cls, NULL, MKDEV(major, 0), NULL, "adc");
	return 0;

err_req_irq:
	free_irq(irq,NULL);

err_clk:
	clk_disable(base_clk);
	clk_unprepare(base_clk);
	return -EINVAL;
	
}

static int adc_remove(struct platform_device *pdev)
{
	printk("enter %s\n", __func__);
	device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);
	cdev_del(&adc_cdev);
	unregister_chrdev_region(MKDEV(major, 0), 1);
	clk_disable(base_clk);
	clk_unprepare(base_clk);
	free_irq(irq, NULL);
	printk("leave %s\n", __func__);
	return 0;
}

static const struct of_device_id adc_dt_ids[] ={
	{.compatible = "tiny4412,adc_demo"},
	{},
};

MODULE_DEVICE_TABLE(of, adc_dt_ids);

static struct platform_driver adc_driver = {
	.driver	= {
		.name	= "adc_demo",
		.of_match_table = of_match_ptr(adc_dt_ids),
	},
	.probe	= adc_probe,
	.remove	= adc_remove,
};

static int adc_init(void)
{
	int ret;
	printk("enter %s\n", __func__);
	ret = platform_driver_register(&adc_driver);

	if(ret) {
		printk(KERN_ERR "adc demo: probe failed error: %d\n", ret);
	}

	return ret;
}

static void adc_exit(void)
{
	printk("enter %s\n", __func__);
	platform_driver_unregister(&adc_driver);
}

module_init(adc_init);
module_exit(adc_exit);
MODULE_LICENSE("GPL");

