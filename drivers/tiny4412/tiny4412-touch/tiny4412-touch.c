#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/of_gpio.h>

#define uchar unsigned char
static void touch_read_handler(struct work_struct *work);
DECLARE_WORK(touch_read_work, touch_read_handler);

static struct i2c_client *touch_client;
static struct input_dev *touch_dev;
static int irq;

static void touch_read(unsigned char sAddr, unsigned char *buf, unsigned int len)
{
	struct i2c_msg msg[2];
	int i, ret;
	unsigned char address;
	for(i = 0; i < len; i++) {
		address = sAddr + i;
		msg[0].addr	= touch_client->addr;
		msg[0].buf 	= &address;
		msg[0].len 	= 1;
		msg[0].flags	= 0;

		msg[1].addr	= touch_client->addr;
		msg[1].buf	= &buf[i];
		msg[1].len	= 1;
		msg[1].flags	= I2C_M_RD;

		ret = i2c_transfer(touch_client->adapter, msg, 2);
		if(ret < 0) {
			printk("i2c_transfer return error\n");
		}

		mdelay(10);
	}
}

static void touch_read_handler(struct work_struct * work)
{
	unsigned char buf[13];
	unsigned char touches, i, event, id;
	unsigned short x, y;
	bool act;

	touch_read(0x00, buf, 13);
	touches = buf[2] & 0x0f;

	if(touches > 2) {
		printk("%s touch read return more than 2 touch point\n", __func__);
		touches = 2;
	}

	for(i = 0; i < touches; i++) {
		y = ((buf[5 + i * 6] & 0x0f) << 8) | buf[6 + i * 6];
		x = ((buf[3 + i * 6] & 0x0f) << 8) | buf[4 + i * 6];
		printk("%d point x:%08d y:%08d\n", i, x, y);

		event = buf[3 + i * 6] >> 6;
		id = buf[5 + i * 6] >> 4;
		printk("%d point event:%d id:%d\n", i, event, id);

		act = (event == 0x00 || event == 0x02 || event == 0x01);

//		input_mt_slot(touch_dev, id);
//		input_mt_report_slot_state(touch_dev, MT_TOOL_FINGER, act);


//		if(!act)
//			continue;

		input_report_abs(touch_dev, ABS_MT_POSITION_X, x);
		input_report_abs(touch_dev, ABS_MT_POSITION_Y, y);

		input_report_abs(touch_dev, ABS_MT_PRESSURE, 200);
		input_report_abs(touch_dev, ABS_MT_TOUCH_MAJOR, 200);
		input_report_abs(touch_dev, ABS_MT_TRACKING_ID, i);

		input_mt_sync(touch_dev);
	}

	input_mt_sync_frame(touch_dev);
	input_sync(touch_dev);
}

static irqreturn_t touch_isr(int irq, void *dev_id)
{
	schedule_work(&touch_read_work);
	return IRQ_HANDLED;
}

static int touch_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	unsigned char buf;
	int ret;

	touch_client = client;
	printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);

	touch_read(0xa3, &buf, 1);
	printk("Chip vendor ID %x\n", buf);

	touch_read(0xa6, &buf, 1);
	printk("Firware ID %x\n", buf);

	touch_read(0xa8, &buf, 1);
	printk("CTPM vendor ID %x\n", buf);

	touch_read(0x00, &buf, 1);
	printk("DEVICE_MODE %x\n", buf);

	touch_read(0x80, &buf, 1);
	printk("ID_G_THGROUP %x\n", buf);

	touch_read(0x88, &buf, 1);
	printk("ID_G_PERIODACTIVE %x\n", buf);

	touch_dev = input_allocate_device();
	if(touch_dev == NULL) {
		printk("%s, allocate input device return error\n", __func__);
		return -1;
	}

	set_bit(EV_SYN, touch_dev->evbit);
	set_bit(EV_ABS, touch_dev->evbit);
	set_bit(EV_KEY, touch_dev->evbit);

	set_bit(ABS_MT_TRACKING_ID, touch_dev->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, touch_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, touch_dev->absbit);
	set_bit(ABS_MT_POSITION_X, touch_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, touch_dev->absbit);

	input_set_abs_params(touch_dev, ABS_MT_POSITION_X, 0, 800, 0, 0);
	input_set_abs_params(touch_dev, ABS_MT_POSITION_Y, 0, 480, 0, 0);
	input_set_abs_params(touch_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0 );
	input_set_abs_params(touch_dev, ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
//	input_set_abs_params(touch_dev, ABS_MT_TRACKING_ID, 0, TOUCH_POINT, 0, 0);
	input_set_abs_params(touch_dev, ABS_MT_TRACKING_ID, 0, 5, 0, 0);
	ret = input_mt_init_slots(touch_dev, 2, INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if(ret) {
		printk("%s, input_mt_init_slots return error\n", __func__);
		return ret;
	}

	touch_dev->name = "touch";
	touch_dev->id.bustype = BUS_I2C;
	touch_dev->dev.parent = &(touch_client)->dev;
	ret = input_register_device(touch_dev);
	if(ret ) {
		printk("%s, input_register_device return error\n", __func__);
		return ret;
	}

	printk("isq is %d\n", irq);

	ret = devm_request_threaded_irq(&touch_client->dev, irq, touch_isr, NULL, IRQF_TRIGGER_FALLING , "touch1", NULL);
	if(ret < 0) {
		printk("failed to request_irq %d\n", ret);
	}

	return 0;
}

static int touch_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id touch_id_table[] = {
	{"touch", 0},
	{}
};

static struct i2c_driver touch_driver = {
	.driver = {
		.name = "touch",
		.owner = THIS_MODULE,
	},
	.probe = touch_probe,
	.remove = touch_remove,
	.id_table = touch_id_table,
};

static int int_demo_remove(struct platform_device *pdev)
{
	printk("%s enter.\n", __func__);
	return 0;
}

static int int_demo_probe(struct platform_device *pdev)
{
	printk("%s enter\n", __func__);
	irq = platform_get_irq(pdev, 0);
	printk("%s platform_get_irq return irq = %d\n", __func__, irq);
	return 0;
}

static const struct of_device_id touch_demo_dt_ids[] = {
	{.compatible = "tiny4412,touch_demo"},
	{},
};
MODULE_DEVICE_TABLE(of, touch_demo_dt_ids);

static struct platform_driver touch_demo_driver = {
	.driver = {
		.name = "touch_demo",
		.owner = THIS_MODULE,
	},
	.probe = int_demo_probe,
	.remove = int_demo_remove,
};

static int touch_drv_init(void) 
{
	int ret;
	printk("%s enter\n", __func__);

	ret = platform_driver_register(&touch_demo_driver);
	if(ret)
		printk(KERN_ERR "int demo: probe failed: %d\n", ret);

	i2c_add_driver(&touch_driver);

	return 0;
}

static void touch_drv_exit(void)
{
	i2c_del_driver(&touch_driver);
	platform_driver_unregister(&touch_demo_driver);
}

module_init(touch_drv_init);
module_exit(touch_drv_exit);
MODULE_LICENSE("GPL");