#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"

int main()
{
    char buf[100000];
    int offset = 5000;

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        long long t1 = write(fd, buf, 2);
        long long t2 = write(fd, buf, 3);
        printf("%d %lld %lld\n", i, t1, t2);
    }


    close(fd);
    return 0;
}
