/* fs.h — 文件系统所有数据结构 */
#ifndef FS_H
#define FS_H

#include "types.h"

/* ================================================================
 * 常量
 * ================================================================ */
#define BSIZE 1024
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define DIRSIZ 14
#define IPB (BSIZE / sizeof(struct dinode))
#define BPB (BSIZE * 8)

#define FSMAGIC 0x10203040
#define ROOTINO 1
#define T_FILE   1
#define T_DIR    2
#define T_DEVICE 3

#define FD_NONE  0
#define FD_INODE 1

/* ================================================================
 * 块缓冲（bio.c）
 * ================================================================ */
struct buf {
  int valid;
  int disk;
  uint dev;
  uint blockno;
  uint refcnt;
  struct buf *prev;
  struct buf *next;
  uchar data[BSIZE];
};

/* ================================================================
 * 磁盘上的数据结构
 * ================================================================ */
struct superblock {
  uint magic;
  uint size;
  uint nblocks;
  uint ninodes;
  uint nlog;
  uint logstart;
  uint inodestart;
  uint bmapstart;
};

struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT + 1];
};

/* ================================================================
 * 内存 inode 缓存
 * ================================================================ */
struct inode {
  uint dev;
  uint inum;
  int ref;
  int valid;
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT + 1];
};

/* ================================================================
 * 目录项
 * ================================================================ */
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

/* ================================================================
 * 文件描述符层
 * ================================================================ */
struct file {
  int type;
  int ref;
  char readable;
  char writable;
  struct inode *ip;
  uint off;
};

#endif
