
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/mod_devicetable.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/property.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/nvmem-provider.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/gpio/consumer.h>

static int major = 0;//1.先确定主设备号，让系统给我们分配
static struct class *ap3216c_class;
static struct i2c_client *ap3216c_client;//定义全局变量记录client！！

static ssize_t ap3216c_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	int err;
	char kernel_buf[6];
	int val;
	
	if (size != 6)
		return -EINVAL;

	val = i2c_smbus_read_word_data(ap3216c_client, 0xA); /* read IR */
	kernel_buf[0] = val & 0xff;//低位
	kernel_buf[1] = (val>>8) & 0xff;//高位
	
	val = i2c_smbus_read_word_data(ap3216c_client, 0xC); /* read 光强 */
	kernel_buf[2] = val & 0xff;
	kernel_buf[3] = (val>>8) & 0xff;

	val = i2c_smbus_read_word_data(ap3216c_client, 0xE); /* read 距离 */
	kernel_buf[4] = val & 0xff;
	kernel_buf[5] = (val>>8) & 0xff;
	
	err = copy_to_user(buf, kernel_buf, size);
	return size;
}


static int ap3216c_open (struct inode *node, struct file *file)
{
	i2c_smbus_write_byte_data(ap3216c_client, 0, 0x4);
	/* delay for reset */
	mdelay(20);
	i2c_smbus_write_byte_data(ap3216c_client, 0, 0x3);
	return 0;
}


static struct file_operations ap3216c_ops = {//2.定义结构体
	.owner = THIS_MODULE,
	.open  = ap3216c_open, //最后实现这些函数
	.read  = ap3216c_read,
};

static const struct of_device_id of_match_ids_ap3216c[] = {
	{ .compatible = "com_name,chip_name",		.data = NULL },
	{ /* END OF LIST */ },
};

static const struct i2c_device_id ap3216c_ids[] = {
	{ "chip_name",	(kernel_ulong_t)NULL },
	{ /* END OF LIST */ }
};

static int ap3216c_probe(struct i2c_client *client)//3.先实现这些函数！！！
{
	printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
	ap3216c_client = client;
	
	/* register_chrdev */
	major = register_chrdev(0, "ap3216c", &ap3216c_ops);

	ap3216c_class = class_create(THIS_MODULE, "ap3216c_class");//这些去看他那个hello的模板
	device_create(ap3216c_class, NULL, MKDEV(major, 0), NULL, "ap3216c"); /* /dev/ap3216c */

	return 0;
}

static int ap3216c_remove(struct i2c_client *client)
{
	printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);//打印语句确定内核已经加载程序
	device_destroy(ap3216c_class, MKDEV(major, 0));
	class_destroy(ap3216c_class);
	
	/* unregister_chrdev */
	unregister_chrdev(major, "ap3216c");

	return 0;
}

static struct i2c_driver i2c_ap3216c_driver = {
	.driver = {
		.name = "ap3216c",
		.of_match_table = of_match_ids_ap3216c,
	},
	.probe_new = ap3216c_probe,
	.remove = ap3216c_remove,
	.id_table = ap3216c_ids,
};


static int __init i2c_driver_ap3216c_init(void)
{
	printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
	return i2c_add_driver(&i2c_ap3216c_driver);
}
module_init(i2c_driver_ap3216c_init);

static void __exit i2c_driver_ap3216c_exit(void)
{
	i2c_del_driver(&i2c_ap3216c_driver);
}
module_exit(i2c_driver_ap3216c_exit);

MODULE_AUTHOR("www.100ask.net");
MODULE_LICENSE("GPL");



