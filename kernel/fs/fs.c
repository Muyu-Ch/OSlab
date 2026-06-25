/* fs.c — 文件系统核心（Lab7 任务2-3）
 *
 * 实现 inode 管理、块分配、文件读写、目录操作、路径解析。
 */

#include "defs.h"
#include "fs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "riscv.h"
#include "types.h"

/* ================================================================
 * 全局状态
 * ================================================================ */
struct superblock sb;

struct {
  struct inode inode[NINODE];
} icache;

/* ================================================================
 * 声明外部函数
 * ================================================================ */
extern void ramdisk_rw(struct buf *b, int write);

/* ================================================================
 * balloc — 分配一个空闲磁盘块
 * ================================================================ */
static uint balloc(uint dev) {
  for (int b = 0; b < sb.size; b += BPB) {
    struct buf *bp = bread(dev, sb.bmapstart + b / BPB);
    for (int bi = 0; bi < BPB && b + bi < sb.size; bi++) {
      int m = bi / 8;
      int bit = bi % 8;
      if ((bp->data[m] & (1 << bit)) == 0) {
        bp->data[m] |= (1 << bit);
        bwrite(bp);
        brelse(bp);
        /* 清零新分配的块 */
        struct buf *zero = bread(dev, b + bi);
        memset(zero->data, 0, BSIZE);
        bwrite(zero);
        brelse(zero);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: no free blocks");
}

/* ================================================================
 * bfree — 释放一个磁盘块
 * ================================================================ */
static void bfree(uint dev, uint bno) {
  struct buf *bp = bread(dev, sb.bmapstart + bno / BPB);
  int bi = bno % BPB;
  bp->data[bi / 8] &= ~(1 << (bi % 8));
  bwrite(bp);
  brelse(bp);
}

/* ================================================================
 * bzero — 清零一个磁盘块
 * ================================================================ */
static void bzero(uint dev, uint bno) {
  struct buf *bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  bwrite(bp);
  brelse(bp);
}

/* ================================================================
 * fsinit — 初始化文件系统
 * ================================================================ */
void fsinit(int dev) {
  /* 初始化缓冲池 */
  binit();

  /* 读取超级块 */
  struct buf *bp = bread(dev, 1);
  memmove(&sb, bp->data, sizeof(sb));
  brelse(bp);

  /* 验证魔数 */
  if (sb.magic != FSMAGIC)
    panic("fsinit: invalid file system");

  /* 初始化 inode 缓存 */
  for (int i = 0; i < NINODE; i++) {
    icache.inode[i].ref = 0;
    icache.inode[i].valid = 0;
  }
}

/* ================================================================
 * iget — 获取 inode 的内存缓存引用（不读磁盘）
 * ================================================================ */
struct inode *iget(uint dev, uint inum) {
  struct inode *ip, *empty;

  empty = 0;
  for (ip = icache.inode; ip < icache.inode + NINODE; ip++) {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
      ip->ref++;
      return ip;
    }
    if (empty == 0 && ip->ref == 0)
      empty = ip;
  }

  if (empty == 0)
    panic("iget: no inodes");

  empty->dev = dev;
  empty->inum = inum;
  empty->ref = 1;
  empty->valid = 0;
  return empty;
}

/* ================================================================
 * ilock — 锁定 inode 并从磁盘加载数据
 * ================================================================ */
void ilock(struct inode *ip) {
  if (ip->valid == 0) {
    struct buf *bp = bread(ip->dev, sb.inodestart + ip->inum / IPB);
    struct dinode *dip = (struct dinode *)bp->data + (ip->inum % IPB);

    ip->type  = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size  = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
  }
  if (ip->type == 0)
    panic("ilock: no type");
}

/* ================================================================
 * iunlock — 解锁 inode
 * ================================================================ */
void iunlock(struct inode *ip) {
  /* 简化版：空操作（未实现睡眠锁）*/
}

/* ================================================================
 * iput — 释放 inode 引用
 * ================================================================ */
void iput(struct inode *ip) {
  ip->ref--;
  if (ip->ref == 0) {
    ip->valid = 0;
  }
}

/* ================================================================
 * iupdate — 将内存 inode 修改写回磁盘
 * ================================================================ */
void iupdate(struct inode *ip) {
  struct buf *bp = bread(ip->dev, sb.inodestart + ip->inum / IPB);
  struct dinode *dip = (struct dinode *)bp->data + (ip->inum % IPB);

  dip->type  = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size  = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  bwrite(bp);
  brelse(bp);
}

/* ================================================================
 * ialloc — 分配一个新 inode
 * ================================================================ */
struct inode *ialloc(uint dev, short type) {
  for (int inum = 1; inum < sb.ninodes; inum++) {
    struct buf *bp = bread(dev, sb.inodestart + inum / IPB);
    struct dinode *dip = (struct dinode *)bp->data + (inum % IPB);
    if (dip->type == 0) {
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      bwrite(bp);
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

/* ================================================================
 * bmap — 将文件的逻辑块号映射到磁盘的物理块号
 * ================================================================ */
uint bmap(struct inode *ip, uint bn) {
  uint addr;
  struct buf *bp;
  uint *a;

  if (bn < NDIRECT) {
    if ((addr = ip->addrs[bn]) == 0) {
      addr = balloc(ip->dev);
      ip->addrs[bn] = addr;
    }
    return addr;
  }

  bn -= NDIRECT;

  if (bn < NINDIRECT) {
    if ((addr = ip->addrs[NDIRECT]) == 0) {
      addr = balloc(ip->dev);
      ip->addrs[NDIRECT] = addr;
    }

    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;

    if ((addr = a[bn]) == 0) {
      addr = balloc(ip->dev);
      a[bn] = addr;
      bwrite(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

/* ================================================================
 * readi — 从 inode 文件中读取数据
 * ================================================================ */
int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n) {
  uint tot, m;
  struct buf *bp;

  if (off > ip->size || off + n < off)
    return 0;
  if (off + n > ip->size)
    n = ip->size - off;

  for (tot = 0; tot < n; tot += m, off += m, dst += m) {
    uint blockno = bmap(ip, off / BSIZE);
    bp = bread(ip->dev, blockno);

    m = BSIZE - off % BSIZE;
    if (m > n - tot)
      m = n - tot;

    if (user_dst) {
      /* 逐字节拷贝到用户地址空间 */
      struct proc *p = myproc();
      for (uint i = 0; i < m; i++) {
        uint64 pa = walkaddr(p->pagetable, dst + i);
        if (pa == 0) { brelse(bp); return -1; }
        *(char *)pa = bp->data[(off % BSIZE) + i];
      }
    } else {
      memmove((void *)dst, bp->data + (off % BSIZE), m);
    }

    brelse(bp);
  }

  return tot;
}

/* ================================================================
 * writei — 向 inode 文件写入数据
 * ================================================================ */
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n) {
  uint tot, m;
  struct buf *bp;

  if (off > ip->size || off + n < off)
    return -1;
  if (off + n > NDIRECT * BSIZE + NINDIRECT * BSIZE)
    return -1;

  for (tot = 0; tot < n; tot += m, off += m, src += m) {
    uint blockno = bmap(ip, off / BSIZE);
    bp = bread(ip->dev, blockno);

    m = BSIZE - off % BSIZE;
    if (m > n - tot)
      m = n - tot;

    if (user_src) {
      /* 逐字节从用户地址空间读取 */
      struct proc *p = myproc();
      for (uint i = 0; i < m; i++) {
        uint64 pa = walkaddr(p->pagetable, src + i);
        if (pa == 0) { brelse(bp); return -1; }
        bp->data[(off % BSIZE) + i] = *(char *)pa;
      }
    } else {
      memmove(bp->data + (off % BSIZE), (void *)src, m);
    }
    bwrite(bp);
    brelse(bp);
  }

  if (off > ip->size)
    ip->size = off;
  iupdate(ip);

  return tot;
}

/* ================================================================
 * dirlookup — 在目录 inode 中按文件名查找
 * ================================================================ */
struct inode *dirlookup(struct inode *dp, char *name, uint *poff) {
  uint off, inum;
  struct dirent de;

  for (off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup: read error");

    if (de.inum == 0)
      continue;

    /* 比较名字（固定比较 DIRSIZ 字节）*/
    int match = 1;
    for (int i = 0; i < DIRSIZ; i++) {
      if (de.name[i] != name[i]) {
        match = 0;
        break;
      }
      if (name[i] == 0)
        break;
    }

    if (match) {
      inum = de.inum;
      if (poff) *poff = off;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

/* ================================================================
 * skipelem — 解析路径的下一个分量
 * ================================================================ */
static char *skipelem(char *path, char *name) {
  /* 跳过开头的 '/' */
  while (*path == '/')
    path++;

  if (*path == 0)
    return 0;

  char *s = path;
  while (*path != '/' && *path != 0)
    path++;

  int len = path - s;
  if (len > DIRSIZ)
    len = DIRSIZ;

  memmove(name, s, len);
  /* 剩余部分填 0 */
  for (int i = len; i < DIRSIZ; i++)
    name[i] = 0;

  /* 跳过路径中间的所有 '/' */
  while (*path == '/')
    path++;

  return path;
}

/* ================================================================
 * namei — 把路径字符串变成 inode
 * ================================================================ */
struct inode *namei(char *path) {
  char name[DIRSIZ];
  struct inode *ip, *next;

  if (*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    return 0;  /* 简化：不支持相对路径 */

  while ((path = skipelem(path, name)) != 0) {
    ilock(ip);
    if (ip->type != T_DIR) {
      iunlock(ip);
      iput(ip);
      return 0;
    }
    next = dirlookup(ip, name, 0);
    iunlock(ip);
    iput(ip);
    if (next == 0)
      return 0;
    ip = next;
  }

  return ip;
}

/* ================================================================
 * nameiparent — 解析路径到父目录，返回路径的文件名部分
 * ================================================================ */
struct inode *nameiparent(char *path, char *name) {
  struct inode *ip, *next;

  if (*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    return 0;

  char name_buf[DIRSIZ];
  char *new_path;

  while (1) {
    char temp[DIRSIZ];
    /* 保存当前 name */
    memmove(temp, name, DIRSIZ);

    new_path = skipelem(path, name_buf);

    if (new_path == 0 || *new_path == 0) {
      /* path 的最后一个分量 → name 是这个分量的名字，ip 是父目录 */
      memmove(name, name_buf, DIRSIZ);
      return ip;
    }

    /* new_path[0] != 0 — 还有更多分量，继续 */
    ilock(ip);
    if (ip->type != T_DIR) {
      iunlock(ip);
      iput(ip);
      return 0;
    }
    next = dirlookup(ip, name_buf, 0);
    iunlock(ip);
    iput(ip);
    if (next == 0)
      return 0;
    ip = next;
    path = new_path;
  }
}

/* ================================================================
 * dirlink — 在目录中添加一条记录
 * ================================================================ */
int dirlink(struct inode *dp, char *name, uint inum) {
  struct dirent de;
  uint off;

  /* 先找空槽（已删除的条目）*/
  for (off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink: read error");
    if (de.inum == 0)
      break;
  }

  /* 填充新条目 */
  memset(&de, 0, sizeof(de));
  de.inum = inum;
  /* 拷贝名字 */
  int i;
  for (i = 0; i < DIRSIZ && name[i]; i++)
    de.name[i] = name[i];
  for (; i < DIRSIZ; i++)
    de.name[i] = 0;

  return writei(dp, 0, (uint64)&de, off, sizeof(de));
}
