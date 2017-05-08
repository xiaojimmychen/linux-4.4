#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>

#define RED_COLOR565 0x0F100
#define GREEN_COLOR565 0x007E0
#define BLUE_COLOR565 0x0001F

int main(int argc, char **argv)
{
	int fb = 0;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	long int screen_size = 0;
	short *fbp565 = NULL;

	int x = 0, y = 0;

	fb = open("/dev/fb0", O_RDWR);
	if(!fb) {
		printf("open /dev/fb0 return error\n");
		return -1;
	}

	if(ioctl(fb, FBIOGET_FSCREENINFO, &finfo)) {
		printf("get fb fixed infomation return error\n");
		return -1;
	}

	if(ioctl(fb, FBIOGET_VSCREENINFO, &vinfo)) {
		printf("get fb variable infomation return error\n");
		return -1;
	}

	screen_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

	printf("%dx%d, %dbpp, screen_size = %d\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, screen_size );

	fbp565 = (short *)mmap(0, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
	if(fbp565 < 0) {
		printf("mmap return error\n");
		return -1;
	}

    if(vinfo.bits_per_pixel == 16)  
    {  
        printf("16 bpp framebuffer\n");  
  
        // Red Screen   
        printf("Red Screen\n");  
        for(y = 0; y < vinfo.yres/3;  y++)  
        {  
            for(x = 0; x < vinfo.xres ; x++)  
            {  
                *(fbp565 + y * vinfo.xres + x) = RED_COLOR565;  
            }  
        }  
  
        // Green Screen   
        printf("Green Screen\n");  
        for(y = vinfo.yres/3; y < (vinfo.yres*2)/3; y++)  
        {  
            for(x = 0; x < vinfo.xres; x++)  
            {  
                *(fbp565 + y * vinfo.xres + x) =GREEN_COLOR565;  
            }  
        }  
  
        // Blue Screen   
        printf("Blue Screen\n");  
        for(y = (vinfo.yres*2)/3; y < vinfo.yres; y++)  
        {  
            for(x = 0; x < vinfo.xres; x++)  
            {  
                *(fbp565 + y * vinfo.xres + x) = BLUE_COLOR565;  
            }  
        }  
    }  
      
    else  
    {  
        printf("warnning: bpp is not 16\n");  
    }  

	munmap(fbp565, screen_size);
	close(fb);
	return 0;
}
