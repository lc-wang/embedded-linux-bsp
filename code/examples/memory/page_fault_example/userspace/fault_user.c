#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define DEV "/dev/myfault"
#define SIZE 4096

int main(void)
{
	int fd;
	char *p;

	fd = open(DEV, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	p = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	printf("mapping done, now accessing memory...\n");

	/* 這一行會觸發 page fault */
	p[0] = 'A';

	printf("write done\n");

	munmap(p, SIZE);
	close(fd);

	return 0;
}
