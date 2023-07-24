/* 为uart_driver下面的每一个port添加一个端口 */
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/rational.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

#include <asm/irq.h>

#define BUF_LEN  1024
#define NEXT_PLACE(i) ((i+1)&0x3FF)

static struct uart_port	*virt_port;
static unsigned char txbuf[BUF_LEN];
static int tx_buf_r = 0;
static int tx_buf_w = 0;

static unsigned char rxbuf[BUF_LEN];
static int rx_buf_r = 0;
static int rx_buf_w = 0;

static struct proc_dir_entry *uart_proc_file;
static int txbuf_put(unsigned char val);
static struct uart_driver virt_uart_drv;


/*
 * Interrupts are disabled on entering
 */
static void virt_uart_console_write(struct console *co, const char *s, unsigned int count)
{
	int i;
	for (i = 0; i < count; i++)
		if (txbuf_put(s[i]) != 0)//把数据存入一个环形缓冲区，如果是真是的硬件就是把数据发到硬件上去
			return;
}
struct tty_driver *virt_uart_console_device(struct console *co, int *index)
{
	struct uart_driver *p = co->data;
	*index = co->index;
	return p->tty_driver;
}


static struct console virt_uart_console = {
	.name		= "ttyVIRT",
	.write		= virt_uart_console_write,
	.device		= virt_uart_console_device,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data       = &virt_uart_drv,
};


static struct uart_driver virt_uart_drv = {
	.owner          = THIS_MODULE,
	.driver_name    = "VIRT_UART",
	.dev_name       = "ttyVIRT",
	.major          = 0,
	.minor          = 0,
	.nr             = 1,
	.cons			= &virt_uart_console,//增加的consol结构体
};

/* circle buffer */


static int is_txbuf_empty(void)
{
	return tx_buf_r == tx_buf_w;
}

static int is_txbuf_full(void)
{
	return NEXT_PLACE(tx_buf_w) == tx_buf_r;
}

static int txbuf_put(unsigned char val)
{
	if (is_txbuf_full())
		return -1;
	txbuf[tx_buf_w] = val;
	tx_buf_w = NEXT_PLACE(tx_buf_w);
	return 0;
}

static int txbuf_get(unsigned char *pval)
{
	if (is_txbuf_empty())
		return -1;
	*pval = g_events[tx_buf_r];
	tx_buf_r = NEXT_PLACE(tx_buf_r);
	return 0;
}

static int txbuf_count(void)
{
	if (tx_buf_w >= tx_buf_r)
		return tx_buf_w - tx_buf_r;
	else
		return BUF_LEN + tx_buf_w - tx_buf_r;
}

ssize_t virt_uart_buf_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	/* 把txbuf中的数据copy_to_user */
	int cnt = txbuf_count();//要读多长
	int i;
	unsigned char val;

	cnt = (cnt > size)? size: cnt;

	for (i = 0; i < cnt; i++)
	{
		txbuf_get(&val);//从环形缓冲区中把数据一个一个取出来
		copy_to_user(buf+i, &val, 1);
	}//这不是高效的方法
	//高效的方法是一次性拷贝到用户空间
	return cnt;
}

static ssize_t virt_uart_buf_write (struct file *file, const char __user *buf, size_t, loff_t *ppos);
{
	/* get data */
	copy_from_user(rxbuf, buf, size);
	rx_buf_w = size;

	/* 模拟产生RX中断 */
	irq_set_irqchip_state(virt_port->irq, IRQCHIP_STATE_PENDING, 1);//表示有中断在等待，
																				//下面要注册一个中断处理函数
	
	return size;
	//return 0;
}

static const struct file_operations virt_uart_buf_fops = {//虽然有这样的一个结构体但是我们要把他用起来
															//创建一个虚拟文件
	.read		= virt_uart_buf_read,
	.write		= virt_uart_buf_write,
};

static unsigned int virt_tx_empty(struct uart_port *port)
{
	/* 因为要发送的数据瞬间存入buffer */
	return 1;
}


/*
 * interrupts disabled on entry
 */
static void virt_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	while (!uart_circ_empty(xmit) &&
	       !uart_tx_stopped(port)) {//环形缓冲区里面有数据并且还没有停止这个串口
		/* send xmit->buf[xmit->tail]
		 * out the port here */

		/* 把circ buffer中的数据全部存入txbuf */

		//txbuf[tx_buf_w++] =  xmit->buf[xmit->tail];
		txbuf_put(xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;//更新统计信息
	}

   if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
	   uart_write_wakeup(port);//如果有其他程序在等待的话我们可以去唤醒他们

}

static void
virt_set_termios(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old)
{
	return;//波特率哪些，这个函数即使没有用也要留着，因为我们不涉及真正的硬件
	//我们就把相关的函数设置为空函数
}


static const struct uart_ops virt_pops = {
	.tx_empty	= virt_tx_empty,//瞬间发送
	//.set_mctrl	= imx_set_mctrl,
	//.get_mctrl	= imx_get_mctrl,
	//.stop_tx	= imx_stop_tx,
	.start_tx	= virt_start_tx,//有数据要发送时调用
	//.stop_rx	= imx_stop_rx,
	//.enable_ms	= imx_enable_ms,
	//.break_ctl	= imx_break_ctl,
	//.startup	= imx_startup,
	//.shutdown	= imx_shutdown,
	//.flush_buffer	= imx_flush_buffer,
	.set_termios	= virt_set_termios,
	//.type		= imx_type,
	//.config_port	= imx_config_port,
	//.verify_port	= imx_verify_port,
};

static irqreturn_t virt_uart_rxint(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned long flags;
	spin_lock_irqsave(&port->lock, flags);//先关中断
	for (i = 0; i < rx_buf_w; i++) {
		port->icount.rx++;//行规程获取数据的同时，会更新统计信息
	
		/* get data from hardware/rxbuf */

		/* put data to ldisc */
		tty_insert_flip_char(port, rxbuf[i], TTY_NORMAL);//放到行规程
	}
	rx_buf_w = 0;//全部写完之后，这个写位置给复位为0
	spin_unlock_irqrestore(&port->lock, flags);//开启中断
	tty_flip_buffer_push(port);//告诉上一层，即行规程来处理
	
	return IRQ_HANDLED;
}

//在probe函数里面我们要去设备树获得硬件信息（去构造和添加uart_port）
//然后去设置uart_port,需要用到uart_ops（波特率、数据位、停止位还有流量控制等等还有读写）
//注册
static int virtual_uart_probe(struct platform_device *pdev)
{	int rxirq;
	int ret;

	/* create proc file */	//把virt_uart_buf_fops这个用起来，创建一个虚拟文件
	uart_proc_file = proc_create("virt_uart_buf", 0, NULL, &virt_uart_buf_fops);
	
	//这个port属于哪一个uart_driver所以在入口函数那里还要注册
	//uart_add_one_port(struct uart_driver * drv, struct uart_port * uport);

	/* 从设备树获得硬件信息 */
	rxirq = platform_get_irq(pdev, 0);
	/* 注册一个中断 */
	ret = devm_request_irq(&pdev->dev, rxirq, imx_rxint, 0,
				       dev_name(&pdev->dev), virt_port);
	
	/* 分配设置注册uart_port */
	virt_port = devm_kzalloc(&pdev->dev, sizeof(*virt_port), GFP_KERNEL);

	virt_port->dev = &pdev->dev;
	virt_port->iotype = UPIO_MEM;
	virt_port->irq = rxirq;
	virt_port->fifosize = 32;
	virt_port->ops = &virt_pops;
	virt_port->flags = 0; // UPF_BOOT_AUTOCONF;
	virt_port->type = PORT_8250;
	virt_port->iobase = 1; /* 为了让uart_configure_port能执行 */
	
	return uart_add_one_port(&virt_uart_drv, virt_port);
}

static int virtual_uart_remove(struct platform_device *pdev)
{

	uart_remove_one_port(&virt_uart_drv, virt_port);
	proc_remove(uart_proc_file);
	return 0;
}

static const struct of_device_id virtual_uart_of_match[] = {
	{ .compatible = "100ask,virtual_uart", },
	{ },
};


static struct platform_driver virtual_uart_driver = {
	.probe		= virtual_uart_probe,
	.remove		= virtual_uart_remove,
	.driver		= {
		.name	= "100ask_virtual_uart",
		.of_match_table = of_match_ptr(virtual_uart_of_match),
	}
};


/* 1. 入口函数 */
static int __init virtual_uart_init(void)
{	
	printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
	
	int ret = uart_register_driver(&virt_uart_drv);//和platform_driver一样
													//我们要去构造一个uart_driver

	if (ret)
		return ret;
	
	/* 1.1 注册一个platform_driver */
	return platform_driver_register(&virtual_uart_driver);
}


/* 2. 出口函数 */
static void __exit virtual_uart_exit(void)
{
	printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
	/* 2.1 反注册platform_driver */
	platform_driver_unregister(&virtual_uart_driver);
}

module_init(virtual_uart_init);
module_exit(virtual_uart_exit);

MODULE_LICENSE("GPL");


