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

#define uchar unsigned char
#define mydebug() printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);

/*
  * 24AA025E48
  * only the low 1k bit can be used
  * so the range can be used is 0 -128 byte
  * one page size is 16 bytes
  * it supports one byte or one page writed
  * it supports one byte or one page readed
  */

static int major;
static struct class *class;
static struct i2c_client *at24cxx_client;

/*
  * input: buf[0] : addr
  * output: buf[1] : len
  */

static ssize_t at24cxx_read(struct file *filep, char __user *buf, size_t count, loff_t *off)
{
	int ret;
	unsigned char addr, len, data[2];
	unsigned char *readbuf;
	struct i2c_msg msg[2];

	if(count != 2) {
		printk("%s count invalid\n", __func__);
		return -EINVAL;
	}

	ret = copy_from_user(data, buf, 2);
	if(ret < 0) {
		printk("copy_from_user return error");
	}

	addr = data[0];
	len = data[1];
	readbuf = kzalloc(len, GFP_KERNEL);

	if(addr + len - 1 >= 128) {
		printk("%s write addr len invalid \n", __func__);
		return -EINVAL;
	}

	if(len == 0) {
		return 0;
	} else {
		readbuf[0] = i2c_smbus_read_byte_data(at24cxx_client, addr);
		mdelay(20);
		addr += 1;
		/* before read data, should send the read addr to it */
		msg[0].addr	= at24cxx_client->addr;
		msg[0].buf	= &addr;
		msg[0].len	= 1;
		msg[0].flags 	= 0;

		/* then read  */
		msg[1].addr	= at24cxx_client->addr;
		msg[1].buf	= readbuf + 1;
		msg[1].len	= len -1;
		msg[1].flags	= I2C_M_RD;

		ret = i2c_transfer(at24cxx_client->adapter, msg, 2);
		if(ret != 2) {
			printk("%s i2c_transfer error\n", __func__);
			return -EINVAL;
		}
	}

	if(data < 0) {
		printk("%s data read error\n", __func__);
	}

	ret = copy_to_user(buf+2, readbuf, len);
	if(ret < 0) {
		printk("%s copy_from_user error\n", __func__);
	}

	kfree(readbuf);
	return count;
}

static void calHead(uchar align, uchar start, uchar len, uchar *hstart, uchar *hlen) 
{
	if(start % align + len <= align) {
		*hlen	= len;
		*hstart	= start;
		return;
	}

	if(start % align == 0) {
		*hlen = 0;
		*hstart = start;
	} else {
		*hlen = align - (start % align);
		*hstart = start;
	}
}

static void calMiddle(uchar align, uchar start, uchar len, uchar *mstart, uchar *mlen, uchar *num)
{
	uchar hstart, hlen;
	calHead(align, start, len, &hstart, &hlen);
	*mstart	= hstart + hlen;
	*num	= (len - hlen) / align;
	*mlen	= ((len - hlen) / align) * align;
}

static void calEnd(uchar align, uchar start, uchar len, uchar *estart, uchar *elen)
{
	uchar hstart, hlen, mstart, mlen, num;
	calHead(align, start, len, &hstart, &hlen);
	calMiddle(align, start, len, &mstart, &mlen, &num);
	*estart = mstart + mlen;
	*elen = len - hlen - mlen;
}
/*
  * buf[0] : addr_start
  * buf[1] : len
  * buf[n] : data
  */
static ssize_t at24cxx_write(struct file *filep, const char __user *buf, size_t count, loff_t *off)
{
	int ret, i;
	unsigned char addr, len;
	unsigned char pagebuff[17];
	unsigned char hstart, hlen, mstart, mlen, num, estart, elen;
	unsigned char *data = kzalloc(count, GFP_KERNEL);
	struct i2c_msg msg;

	if(count < 3) {
		printk("%s count invalid \n", __func__);
		return -EINVAL;
	}

	ret  = copy_from_user(data, buf, count);
	if(ret < 0) {
		printk("%s copy_from_user return error, error NO:%d", __func__, ret);
		return -EINVAL;
	}

	addr = data[0];
	len	= data[1];
	printk("addr = %d, len = %d\n", addr, len);

	if(addr + len - 1 > 128) {
		printk("%s write addr len invalid\n", __func__);
		return -EINVAL;
	}

	calHead(16, addr, len, &hstart, &hlen);
	calMiddle(16, addr, len, &mstart, &mlen, &num);
	calEnd(16, addr, len, &estart, &elen);

	for(i = hstart; i < hstart + hlen; i++) {
		if(i2c_smbus_write_byte_data(at24cxx_client, i, data[2 + i - addr]) < 0) {
			printk("%s i2c_smbus_write_byte_data %d\n", __func__, i);
			return -EINVAL;
		}
		mdelay(5);
	}

	for(i = mstart; i < mstart + mlen; i += 16) {
		memset(pagebuff, i, 1);
		memcpy(pagebuff + 1, data + 2 + i - addr, 16);
		msg.addr = at24cxx_client->addr;
		msg.buf = pagebuff;
		msg.len = 17;
		msg.flags = 0;
		ret = i2c_transfer(at24cxx_client->adapter, &msg, 1);
		if(ret != 1) {
			printk("%s i2c_transfer return error\n", __func__);
			return -EINVAL;
		}

		mdelay(5);
	}

	for(i = estart; i < estart + elen; i++) {
		if(i2c_smbus_write_byte_data(at24cxx_client, i, data[2 + i - addr]) < 0) {
			printk("%s i2c_smbus_write_byte_data %d\n", __func__, i);
			return -EINVAL;
		}

		mdelay(5);
	}

	kfree(data);
	return count;
}

static struct file_operations at24cxx_fops = {
	.owner	= THIS_MODULE,
	.read	= at24cxx_read,
	.write	= at24cxx_write,
};

static int at24cxx_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	mydebug();
	at24cxx_client = client;
	major = register_chrdev(0, "at24cxx", &at24cxx_fops);
	class = class_create(THIS_MODULE, "at24cxx");
	device_create(class, NULL, MKDEV(major, 0), NULL, "at24cxx");
	return 0;
}

static int at24cxx_remove(struct i2c_client *client)
{
	device_destroy(class, MKDEV(major, 0));
	class_destroy(class);
	unregister_chrdev(major, "at24cxx");
	return 0;
}

static const struct i2c_device_id at24cxx_id_table[] = {
	{"eeprom", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, at24cxx_id_table);

static struct i2c_driver at24cxx_driver = {
	.driver = {
		.name	= "eeprom",
		.owner	= THIS_MODULE,
	},
	.probe	= at24cxx_probe,
	.remove 	= at24cxx_remove,
	.id_table	= at24cxx_id_table,
};

static int at24cxx_drv_init(void)
{
	mydebug();
	i2c_add_driver(&at24cxx_driver);
	return 0;
}

static void at24cxx_drv_exit(void)
{
	mydebug();
	i2c_del_driver(&at24cxx_driver);
}

module_init(at24cxx_drv_init);
module_exit(at24cxx_drv_exit);
MODULE_LICENSE("GPL");

