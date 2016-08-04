#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define MAGIC 'i'
#define GET_TEMP _IO(MAGIC, 0)
#define DO_SOMETHING _IO(MAGIC, 1)

int main(void)
{
	int fd,ret;
	printf("opening /dev/awesome\n");
	fd = open("/dev/awesome", O_RDWR);
	printf("/dev/awesome fd: %d\n", fd);
	ioctl(fd, GET_TEMP);
}