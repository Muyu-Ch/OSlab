int getpid(void);
int write(int fd, const void *buf, int count);
int fork(void);
int wait(int *status);
void exit(int code);

int main() {
    write(1, "Start\n", 6);

    int child = fork();
    if (child == 0) {
        /* 子进程：不写任何东西，直接退出 */
        exit(2);
    } else {
        /* 父进程 */
        write(1, "Parent waiting\n", 15);
        int status;
        int cpid = wait(&status);
        write(1, "Parent done\n", 12);
    }

    exit(0);
    return 0;
}
