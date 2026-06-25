/* sysfile.c — 文件相关系统调用（Lab7 Layer 5）*/

#include "defs.h"
#include "param.h"
#include "proc.h"
#include "types.h"

/* 打开文件标志 */
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200

/* 外部函数声明 */
extern struct inode *namei(char *path);
extern struct inode *nameiparent(char *path, char *name);
extern struct inode *ialloc(uint dev, short type);
extern int dirlink(struct inode *dp, char *name, uint inum);
extern int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n);
extern int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n);
extern void ilock(struct inode *ip);
extern void iunlock(struct inode *ip);
extern void iput(struct inode *ip);
extern void iupdate(struct inode *ip);
extern struct file *filealloc(void);
extern struct file *filedup(struct file *f);
extern void fileclose(struct file *f);
extern int fileread(struct file *f, uint64 addr, int n);
extern int filewrite(struct file *f, uint64 addr, int n);

/* ================================================================
 * copyinstr — 从用户虚拟地址拷贝字符串到内核缓冲区
 *
 * 简化版：内核和用户共用页表但有独立映射，
 * 使用 walkaddr 逐字节查找物理地址。
 * ================================================================ */
static int copyinstr(uint64 uva, char *dst, int max) {
  struct proc *p = myproc();
  for (int i = 0; i < max; i++) {
    uint64 pa = walkaddr(p->pagetable, uva + i);
    if (pa == 0)
      return -1;
    dst[i] = *(char *)pa;
    if (dst[i] == 0)
      return i + 1;
  }
  return -1;  /* 未找到 null 终止符 */
}

/* ================================================================
 * copyout — 从内核缓冲区拷贝数据到用户虚拟地址
 * ================================================================ */
static int copyout(uint64 uva, char *src, int n) {
  struct proc *p = myproc();
  for (int i = 0; i < n; i++) {
    uint64 pa = walkaddr(p->pagetable, uva + i);
    if (pa == 0)
      return -1;
    *(char *)pa = src[i];
  }
  return n;
}

/* ================================================================
 * fdalloc — 在进程的 ofile[] 中找一个空闲 fd 槽位
 * ================================================================ */
static int fdalloc(struct file *f) {
  struct proc *p = myproc();
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

/* ================================================================
 * sys_open — 打开或创建文件
 * ================================================================ */
uint64 sys_open(void) {
  char path[MAXPATH];
  int omode;
  struct file *f;
  struct inode *ip;

  /* 从用户态读参数 */
  argint(1, &omode);

  /* 拷贝路径字符串 */
  argstr(0, path, MAXPATH);
  if (path[0] == 0)
    return -1;

  if (omode & O_CREATE) {
    /* 创建文件 */
    char name[14];
    struct inode *dp = nameiparent(path, name);
    if (dp == 0)
      return -1;

    ilock(dp);

    /* 在父目录中查找：文件是否已存在？*/
    if ((ip = dirlookup(dp, name, 0)) != 0) {
      /* 文件已存在，直接使用 */
      iunlock(dp);
      iput(dp);
    } else {
      /* 不存在，创建新 inode */
      ip = ialloc(dp->dev, 1);  /* T_FILE = 1 */
      dirlink(dp, name, ip->inum);
      iunlock(dp);
      iput(dp);
    }
  } else {
    /* 普通打开：用路径查找 */
    if ((ip = namei(path)) == 0)
      return -1;
  }

  /* 锁定 inode，验证类型 */
  ilock(ip);

  /* 分配 file 对象 */
  if ((f = filealloc()) == 0) {
    iunlock(ip);
    iput(ip);
    return -1;
  }

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  iunlock(ip);

  /* 分配 fd */
  int fd = fdalloc(f);
  if (fd < 0) {
    fileclose(f);
    return -1;
  }

  return fd;
}

/* ================================================================
 * sys_read — 读取文件
 * ================================================================ */
uint64 sys_read(void) {
  int fd, n;
  uint64 addr;
  struct file *f;

  argint(0, &fd);
  argaddr(1, &addr);
  argint(2, &n);

  if (fd < 0 || fd >= NOFILE)
    return -1;

  struct proc *p = myproc();
  f = p->ofile[fd];
  if (f == 0)
    return -1;

  return fileread(f, addr, n);
}

/* ================================================================
 * sys_write — 写入文件（或串口）
 * ================================================================ */
uint64 sys_write(void) {
  int fd, n;
  uint64 addr;
  struct file *f;

  argint(0, &fd);
  argaddr(1, &addr);
  argint(2, &n);

  if (fd < 0 || fd >= NOFILE)
    return -1;

  struct proc *p = myproc();

  /* fd==1: 标准输出 → 直接写串口 */
  if (fd == 1) {
    for (int i = 0; i < n; i++) {
      uint64 pa = walkaddr(p->pagetable, addr + i);
      if (pa == 0)
        return -1;
      uart_putc(*(char *)pa);
    }
    return n;
  }

  f = p->ofile[fd];
  if (f == 0)
    return -1;

  return filewrite(f, addr, n);
}

/* ================================================================
 * sys_mkdir — 创建目录
 * ================================================================ */
uint64 sys_mkdir(void) {
  char path[MAXPATH];
  char name[14];
  struct inode *dp, *ip;

  argstr(0, path, MAXPATH);
  if (path[0] == 0) return -1;

  dp = nameiparent(path, name);
  if (dp == 0) return -1;

  ilock(dp);

  /* 确认目录不存在 */
  if (dirlookup(dp, name, 0) != 0) {
    iunlock(dp); iput(dp);
    return -1;  /* 已存在 */
  }

  /* 分配新目录 inode（type=T_DIR=2）*/
  ip = ialloc(dp->dev, T_DIR);

  /* 在父目录中登记 */
  dirlink(dp, name, ip->inum);
  iunlock(dp); iput(dp);

  /* 给新目录加 "." 和 ".." */
  ilock(ip);
  dirlink(ip, ".",  ip->inum);    /* "." 指向自己 */
  dirlink(ip, "..", dp->inum);    /* ".." 指向父目录 */
  ip->nlink = 2;                   /* "." + 父目录的引用 */
  iupdate(ip);
  iunlock(ip);
  iput(ip);

  return 0;
}

/* ================================================================
 * sys_close — 关闭文件
 * ================================================================ */
uint64 sys_close(void) {
  int fd;
  struct file *f;

  argint(0, &fd);

  if (fd < 0 || fd >= NOFILE)
    return -1;

  struct proc *p = myproc();
  f = p->ofile[fd];
  if (f == 0)
    return -1;

  p->ofile[fd] = 0;
  fileclose(f);

  return 0;
}
