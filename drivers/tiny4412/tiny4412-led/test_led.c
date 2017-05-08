#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>

int main(int argc, char **argv)
{
	char led_path[32];
	int on_off = 0;
	int fd = 0;
	int error = 0;
	int read_value = 0;
	
	if(argc != 3) {
		printf("Usage <LED_NUM> 1|0 \n");
		return -1;
	}

	sprintf(led_path, "/dev/%s", argv[1]);
	printf("The led needed to be opened is %s\n", led_path);
	
	on_off = (int)strtol(argv[2], NULL, 0);
	printf("The led will be %s\n", (on_off ?  "on" : "off"));

	fd = open(led_path, O_RDWR);
	if(fd < 0) {
		printf("open %s fail\n", led_path);
		return -1;
	}

	error = write(fd, &on_off, 1);
	if (error < 0) {
		printf("write %s error, error NO: %d\n", led_path, error);
		return -1;
	}

	error = read(fd, &read_value, 1);
	if(error < 0) {
		printf("read %s error, error NO: %d\n", led_path, error);
		return -1;
	}
	
	printf("value read from %s is %d\n", led_path, read_value);

	close(fd);	
	return 0;	
}
