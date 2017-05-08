#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/div64.h>

#include <asm/mach/map.h>
#include <linux/fb.h>
#include <asm/types.h>

#define 	VIDCON0  		0x00
#define 	VIDCON1  		0x04
#define 	VIDTCON0  		0x10
#define 	VIDTCON1  		0x14
#define 	VIDTCON2  		0x18
#define 	WINCON0  		0x20
#define 	VIDOSD0C  		0x48
#define 	SHADOWCON  		0x34
#define 	WINCHMAP2  		0x3c
#define 	VIDOSD0A  		0x40
#define 	VIDOSD0B  		0x44
#define 	VIDW00ADD0B0  	0xA0
#define 	VIDW00ADD1B0  	0xD0

#define 	CLK_SRC_LCD0		0x234
#define 	CLK_SRC_MASK_LCD	0x334
#define 	CLK_DIV_LCD			0x534
#define 	CLK_GATE_IP_LCD		0x934

#define 	LCDBLK_CFG 		0x00
#define		LCDBLK_CFG2		0x04

static int s3c_lcdfb_setcolreg(unsigned int regno, unsigned int red,
                               unsigned int green, unsigned int blue,
                               unsigned int transp, struct fb_info *info);


static struct fb_ops s3c_lcdfb_ops =
{
    .owner		= THIS_MODULE,
    .fb_setcolreg	= s3c_lcdfb_setcolreg,
    .fb_fillrect	= cfb_fillrect,
    .fb_copyarea	= cfb_copyarea,
    .fb_imageblit	= cfb_imageblit,
};


static struct fb_info *s3c_lcd;
static volatile void __iomem *lcd_regs_base;
static volatile void __iomem *clk_regs_base;
static volatile void __iomem *lcdblk_regs_base;
static u32 pseudo_palette[16];
static struct resource *res1, *res2, *res3, *res4;

/* from pxafb.c */
static inline unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
    chan &= 0xffff;
    chan >>= 16 - bf->length;
    return chan << bf->offset;
}


static int s3c_lcdfb_setcolreg(unsigned int regno, unsigned int red,
                               unsigned int green, unsigned int blue,
                               unsigned int transp, struct fb_info *info)
{
    unsigned int val;

    if (regno > 16)
        { return 1; }

    /* 用red,green,blue三原色构造出val */
    val  = chan_to_field(red,	&info->var.red);
    val |= chan_to_field(green, &info->var.green);
    val |= chan_to_field(blue,	&info->var.blue);
    //((u32 *)(info->pseudo_palette))[regno] = val;
    pseudo_palette[regno] = val;
    return 0;
}

static int lcd_probe(struct platform_device *pdev)
{
    volatile unsigned int *LCD0_CONFIGURATION;
    int ret;
    unsigned int temp;
    /* 1. 分配一个fb_info */
    s3c_lcd = framebuffer_alloc(0, NULL);
    /* 2. 设置 */
    /* 2.1 设置 fix 固定的参数 */
    strcpy(s3c_lcd->fix.id, "mylcd");
    s3c_lcd->fix.smem_len = 480 * 800 * 16 / 8;	    //显存的长度
    s3c_lcd->fix.type     = FB_TYPE_PACKED_PIXELS;	//类型
    s3c_lcd->fix.visual   = FB_VISUAL_TRUECOLOR; 	//TFT 真彩色
    s3c_lcd->fix.line_length = 800 * 2;				//一行的长度
    /* 2.2 设置 var 可变的参数 */
    s3c_lcd->var.xres           = 800;	//x方向分辨率
    s3c_lcd->var.yres           = 480;	//y方向分辨率
    s3c_lcd->var.xres_virtual   = 800;	//x方向虚拟分辨率
    s3c_lcd->var.yres_virtual   = 480;	//y方向虚拟分辨率
    s3c_lcd->var.bits_per_pixel = 16;	//每个像素占的bit
    /* RGB:565 */
    s3c_lcd->var.red.offset     = 11;	//红
    s3c_lcd->var.red.length     = 5;
    s3c_lcd->var.green.offset   = 5;	//绿
    s3c_lcd->var.green.length   = 6;
    s3c_lcd->var.blue.offset    = 0;	//蓝
    s3c_lcd->var.blue.length    = 5;
    s3c_lcd->var.activate       = FB_ACTIVATE_NOW;
    /* 2.3 设置操作函数 */
    s3c_lcd->fbops              = &s3c_lcdfb_ops;
    /* 2.4 其他的设置 */
    s3c_lcd->pseudo_palette = pseudo_palette;		//调色板
    //s3c_lcd->screen_base  = ;  					//显存的虚拟地址,分配显存时填充
    s3c_lcd->screen_size    = 480 * 800 * 16 / 8;	//显存大小
    /* 3. 硬件相关的操作 */
    /* 3.1 配置GPIO用于LCD */
    //设备树中使用"default"
    /* 3.2 根据LCD手册设置LCD控制器, 比如VCLK的频率等 */
    //寄存器映射
    res1 = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (res1 == NULL)
    {
        printk("platform_get_resource error\n");
        return -EINVAL;
    }

    lcd_regs_base = devm_ioremap_resource(&pdev->dev, res1);

    if (lcd_regs_base == NULL)
    {
        printk("devm_ioremap_resource error\n");
        return -EINVAL;
    }

    res2 = platform_get_resource(pdev, IORESOURCE_MEM, 1);

    if (res2 == NULL)
    {
        printk("platform_get_resource error\n");
        return -EINVAL;
    }

    lcdblk_regs_base = devm_ioremap_resource(&pdev->dev, res2);

    if (lcdblk_regs_base == NULL)
    {
        printk("devm_ioremap_resource error\n");
        return -EINVAL;
    }

    res3 = platform_get_resource(pdev, IORESOURCE_MEM, 2);

    if (res3 == NULL)
    {
        printk("platform_get_resource error\n");
        return -EINVAL;
    }

    LCD0_CONFIGURATION = ioremap(res3->start, 0x04);
    *LCD0_CONFIGURATION = 0x07;
	
    res4 = platform_get_resource(pdev, IORESOURCE_MEM, 3);

    if (res4 == NULL)
    {
        printk("platform_get_resource error\n");
        return -EINVAL;
    }

    clk_regs_base = ioremap(res4->start, 0x1000);

    if (clk_regs_base == NULL)
    {
        printk("devm_ioremap_resource error\n");
        return -EINVAL;
    }
	
    //使能时钟
    //时钟源选择 0110  SCLKMPLL_USER_T 800M
    temp = readl(clk_regs_base + CLK_SRC_LCD0);
    temp &= ~0x0f;
    temp |= 0x06;
    writel(temp, clk_regs_base + CLK_SRC_LCD0);
    //FIMD0_MASK
    temp = readl(clk_regs_base + CLK_SRC_MASK_LCD);
    temp |= 0x01;
    writel(temp, clk_regs_base + CLK_SRC_MASK_LCD);
    //SCLK_FIMD0 = MOUTFIMD0/(FIMD0_RATIO + 1),分频 1/1
    temp = readl(clk_regs_base + CLK_DIV_LCD);
    temp &= ~0x0f;
    writel(temp, clk_regs_base + CLK_DIV_LCD);
    //CLK_FIMD0 Pass
    temp = readl(clk_regs_base + CLK_GATE_IP_LCD);
    temp |= 0x01;
    writel(temp, clk_regs_base + CLK_GATE_IP_LCD);
    //FIMDBYPASS_LBLK0 FIMD Bypass
    temp = readl(lcdblk_regs_base + LCDBLK_CFG);
    temp |= 1 << 1;
    writel(temp, lcdblk_regs_base + LCDBLK_CFG);
    temp = readl(lcdblk_regs_base + LCDBLK_CFG2);
    temp |= 1 << 0;
    writel(temp, lcdblk_regs_base + LCDBLK_CFG2);
    mdelay(1000);
    //分频	800/(23 +1 ) == 33M
    temp = readl(lcd_regs_base + VIDCON0);
    temp |= (23 << 6);
    writel(temp, lcd_regs_base + VIDCON0);
    /*
     * VIDTCON1:
     * [5]:IVSYNC  ===> 1 : Inverted(反转)
     * [6]:IHSYNC  ===> 1 : Inverted(反转)
     * [7]:IVCLK   ===> 1 : Fetches video data at VCLK rising edge (下降沿触发)
     * [10:9]:FIXVCLK  ====> 01 : VCLK running
     */
    temp = readl(lcd_regs_base + VIDCON1);
    temp |= (1 << 9) | (1 << 7) | (1 << 5) | (1 << 6);
    writel(temp, lcd_regs_base + VIDCON1);
    /*
     * VIDTCON0:
     * [23:16]:  VBPD + 1  <------> tvpw (1 - 20)  13
     * [15:8] :  VFPD + 1  <------> tvfp 22
     * [7:0]  :  VSPW  + 1 <------> tvb - tvpw = 23 - 13 = 10
     */
    temp = readl(lcd_regs_base + VIDTCON0);
    temp |= (12 << 16) | (21 << 8) | (9);
    writel(temp, lcd_regs_base + VIDTCON0);
    /*
     * VIDTCON1:
     * [23:16]:  HBPD + 1  <------> thpw (1 - 40)  36
     * [15:8] :  HFPD + 1  <------> thfp 210
     * [7:0]  :  HSPW  + 1 <------> thb - thpw = 46 - 36 = 10
     */
    temp = readl(lcd_regs_base + VIDTCON1);
    temp |= (35 << 16) | (209 << 8)  | (9);
    writel(temp, lcd_regs_base + VIDTCON1);
    /*
     * HOZVAL = (Horizontal display size) - 1 and LINEVAL = (Vertical display size) - 1.
     * Horizontal(水平) display size : 800
     * Vertical(垂直) display size : 480
     */
    temp = (479 << 11) | 799;
    writel(temp, lcd_regs_base + VIDTCON2);
    /*
     * WINCON0:
     * [16]:Specifies Half-Word swap control bit.  1 = Enables swap P1779 低位像素存放在低字节
     * [5:2]: Selects Bits Per Pixel (BPP) mode for Window image : 0101 ===> 16BPP RGB565
     * [1]:Enables/disables video output   1 = Enables
     */
    temp = readl(lcd_regs_base + WINCON0);
    temp |= (1 << 16) | (5 << 2) | 1;
    writel(temp, lcd_regs_base + WINCON0);
    //Window Size For example, Height ? Width (number of word)
    temp = 480 * 800 >> 1;
    writel(temp, lcd_regs_base + VIDOSD0C);
    temp = readl(lcd_regs_base + SHADOWCON);
    writel(temp | 0x01, lcd_regs_base + SHADOWCON);
    //p1769
    temp = readl(lcd_regs_base + WINCHMAP2);
    temp &= ~(7 << 16);
    temp |= 1 << 16;
    temp &= ~7;
    temp |= 1;
    writel(temp, lcd_regs_base + WINCHMAP2);
    /*
     * bit0-10 : 指定OSD图像左上像素的垂直屏幕坐标
     * bit11-21: 指定OSD图像左上像素的水平屏幕坐标
     */
    writel(0, lcd_regs_base + VIDOSD0A);
    /*
     * bit0-10 : 指定OSD图像右下像素的垂直屏幕坐标
     * bit11-21: 指定OSD图像右下像素的水平屏幕坐标
     */
    writel((799 << 11) | 479, lcd_regs_base + VIDOSD0B);
    /* 3.3 分配显存(framebuffer), 并把地址告诉LCD控制器 */
    // s3c_lcd->screen_base 	显存虚拟地址
    // s3c_lcd->fix.smem_len 	显存大小，前面计算的
    // s3c_lcd->fix.smem_start 	显存物理地址
    s3c_lcd->screen_base = dma_alloc_writecombine(NULL, s3c_lcd->fix.smem_len, (dma_addr_t *)&s3c_lcd->fix.smem_start, GFP_KERNEL);
    //显存起始地址
    writel(s3c_lcd->fix.smem_start, lcd_regs_base + VIDW00ADD0B0);
    //显存结束地址
    writel(s3c_lcd->fix.smem_start + s3c_lcd->fix.smem_len, lcd_regs_base + VIDW00ADD1B0);
    //Enables video output and logic immediately
    temp = readl(lcd_regs_base + VIDCON0);
    writel(temp | 0x03, lcd_regs_base + VIDCON0);
    /* 4. 注册 */
    ret = register_framebuffer(s3c_lcd);
    return ret;
}

static int lcd_remove(struct platform_device *pdev)
{
    printk("%s enter.\n", __func__);
    unregister_framebuffer(s3c_lcd);
    dma_free_writecombine(NULL, s3c_lcd->fix.smem_len, s3c_lcd->screen_base, s3c_lcd->fix.smem_start);
//    iounmap(lcd_regs);
//    iounmap(gpbcon);
//    iounmap(gpccon);
//    iounmap(gpdcon);
//    iounmap(gpgcon);
    framebuffer_release(s3c_lcd);
    return 0;
}

static const struct of_device_id lcd_dt_ids[] =
{
    { .compatible = "tiny4412,lcd_demo", },
    {},
};

MODULE_DEVICE_TABLE(of, lcd_dt_ids);

static struct platform_driver lcd_driver =
{
    .driver        = {
        .name      = "lcd_demo",
        .of_match_table    = of_match_ptr(lcd_dt_ids),
    },
    .probe         = lcd_probe,
    .remove        = lcd_remove,
};

static int lcd_init(void)
{
    int ret;
    printk("enter %s\n", __func__);
    ret = platform_driver_register(&lcd_driver);

    if (ret)
    {
        printk(KERN_ERR "pwm demo: probe faipwm: %d\n", ret);
    }

    return ret;
}

static void lcd_exit(void)
{
    printk("enter %s\n", __func__);
    platform_driver_unregister(&lcd_driver);
}

module_init(lcd_init);
module_exit(lcd_exit);

MODULE_LICENSE("GPL");

