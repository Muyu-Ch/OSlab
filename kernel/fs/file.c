/* file.c — 文件描述符层（Lab7 Layer 4）*/

#include "defs.h"
#include "param.h"
#include "proc.h"
#include "types.h"

/* 全局文件表 */
struct {
  struct file file[NFILE];
} ftable;

/* 外部函数声明 */
extern void ilock(struct inode *ip);
extern void iunlock(struct inode *ip);
extern void iput(struct inode *ip);
extern int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n);
extern int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n);

/* ================================================================
 * fileinit — 初始化文件表
 * ================================================================ */
void fileinit(void) {
  for (int i = 0; i < NFILE; i++)
    ftable.file[i].type = FD_NONE;
}

/* ================================================================
 * filealloc — 从文件表分配一个空闲 file 对象
 * ================================================================ */
struct file *filealloc(void) {
  for (int i = 0; i < NFILE; i++) {
    if (ftable.file[i].ref == 0) {
      ftable.file[i].ref = 1;
      ftable.file[i].type = FD_NONE;
      return &ftable.file[i];
    }
  }
  return 0;
}

/* ================================================================
 * filedup — 复制一个 file（增加引用计数）
 * ================================================================ */
struct file *filedup(struct file *f) {
  if (f->ref < 1)
    panic("filedup");
  f->ref++;
  return f;
}

/* ================================================================
 * fileclose — 关闭文件，释放引用
 * ================================================================ */
void fileclose(struct file *f) {
  if (f->ref < 1)
    panic("fileclose");
  f->ref--;
  if (f->ref > 0)
    return;

  /* ref 归零，真正释放 */
  if (f->type == FD_INODE) {
    iput(f->ip);
  }
  f->type = FD_NONE;
  f->ip = 0;
  f->off = 0;
}

/* ================================================================
 * fileread — 从文件读取数据
 * ================================================================ */
int fileread(struct file *f, uint64 addr, int n) {
  int r = 0;

  if (!f->readable)
    return -1;

  if (f->type == FD_INODE) {
    ilock(f->ip);
    if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }

  panic("fileread: unknown type");
}

/* ================================================================
 * filewrite — 向文件写入数据
 * ================================================================ */
int filewrite(struct file *f, uint64 addr, int n) {
  int r = 0;

  if (!f->writable)
    return -1;

  if (f->type == FD_INODE) {
    ilock(f->ip);
    if ((r = writei(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }

  panic("filewrite: unknown type");
}
