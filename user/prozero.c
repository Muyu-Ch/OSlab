/* prozero.c — 用户测试程序
 *
 * 切换方法：注释/取消注释对应段落即可展示不同实验成果
 */

int getpid(void);
int write(int fd, const void *buf, int count);
int fork(void);
int wait(int *status);
int read(int fd, void *buf, int count);
int open(const char *path, int omode);
int close(int fd);
int mkdir(const char *path);
void exit(int code);

#define LAB7_TEST

#ifdef LAB7_TEST
#define O_CREATE  0x200
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#endif

int main() {

#ifdef LAB6_TEST
    write(1, "Lab6: Multi-Process\n", 19);

    int child = fork();
    if (child == 0) {
        write(1, "I'm Child\n", 9);
        exit(2);
    } else {
        write(1, "Parent waiting\n", 15);
        int status;
        int cpid = wait(&status);
        write(1, "Parent done\n", 12);
    }
#endif

#ifdef LAB7_TEST
    write(1, "Lab7: FS Test\n", 14);

    if (mkdir("/mydir") < 0) {
        write(1, "FAIL mkdir\n", 11); exit(1);
    }

    int fd = open("/hello1", O_CREATE | O_WRONLY);
    write(fd, "Hello!", 6);
    close(fd);

    fd = open("/mydir/hello", O_CREATE | O_WRONLY);
    write(fd, "Hello File System!", 18);
    close(fd);

    fd = open("/hello1", O_RDONLY);
    char buf[32];
    int n = read(fd, buf, 32);
    close(fd);
    write(1, "/hello1: ", 9);
    write(1, buf, n);
    write(1, "\n", 1);

    fd = open("/mydir/hello", O_RDONLY);
    n = read(fd, buf, 32);
    close(fd);
    write(1, "/mydir/hello: ", 15);
    write(1, buf, n);
    write(1, "\n", 1);

    write(1, "ALL PASSED\n", 11);
#endif

    exit(0);
    return 0;
}
