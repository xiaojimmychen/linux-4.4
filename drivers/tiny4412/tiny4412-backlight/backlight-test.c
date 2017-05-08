#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>

#define DRIVER_NAME "/dev/backlight"

int main(int argc, char **argv)
{

	int fd = -1;
	int ret = -1;
	unsigned char backlight_level = 0;

	if(argc != 2) {
		printf("usage: ./backlight-test backlight_level\n");
		return 0;
	}

	backlight_level = (unsigned char)atoi(argv[1]);
	printf("backlight_level = %d\n", backlight_level);

	fd = open(DRIVER_NAME, O_RDWR);
	if(fd < 0) {
		printf("open driver %s return error!\n", DRIVER_NAME);
		return -1;
	}

	ret = write(fd, &backlight_level, 1);
	if(ret < 0) {
		printf("write fd return error\n");
		return -1;
	}

	return 0;
	
}
