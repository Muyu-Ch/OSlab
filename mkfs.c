/* mkfs.c — 创建 xv6 文件系统磁盘镜像
 *
 * 编译：gcc mkfs.c -o mkfs
 * 用法：./mkfs fs.img
 *
 * 生成一个包含基本文件系统结构的磁盘镜像：
 *   Block 0:  boot block（未使用）
 *   Block 1:  superblock
 *   Block 2-31: 日志区（30块）
 *   Block 32-44: inode 区（13块 = 416 inodes）
 *   Block 45:    位图区（1块 = 管理 8192 块）
 *   Block 46+:   数据区
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#define BSIZE   1024
#define NDIRECT 12
#define DIRSIZ  14
#define IPB     (BSIZE / sizeof(struct dinode))

#define FSMAGIC 0x10203040
#define ROOTINO 1
#define T_DIR   2

/* 磁盘上 inode 结构 */
struct dinode {
  int16_t type;
  int16_t major;
  int16_t minor;
  int16_t nlink;
  uint32_t size;
  uint32_t addrs[NDIRECT + 1];
};

/* 超级块 */
struct superblock {
  uint32_t magic;
  uint32_t size;
  uint32_t nblocks;
  uint32_t ninodes;
  uint32_t nlog;
  uint32_t logstart;
  uint32_t inodestart;
  uint32_t bmapstart;
};

/* 目录项 */
struct dirent {
  uint16_t inum;
  char name[DIRSIZ];
};

/* 布局参数 */
#define FSSIZE   1000
#define NLOG       30
#define NINODES   200

#define LOGSTART    2
#define INODESTART  (LOGSTART + NLOG)          /* 2 + 30 = 32 */
#define BMAPSTART   (INODESTART + NINODES/IPB + 1) /* 32 + 13 = 45 */

uint8_t disk[FSSIZE][BSIZE];
uint8_t iblock_dirty[(FSSIZE) / 8];

static void wsect(uint32_t blockno, void *buf) {
  memcpy(disk[blockno], buf, BSIZE);
}

static void winode(uint32_t inum, struct dinode *dip) {
  uint8_t buf[BSIZE];
  uint32_t bn = INODESTART + inum / IPB;
  uint32_t off = (inum % IPB) * sizeof(struct dinode);

  memcpy(buf, disk[bn], BSIZE);
  memcpy(buf + off, dip, sizeof(*dip));
  wsect(bn, buf);
}

static void iappend(uint32_t inum, uint8_t *xp, uint32_t n) {
  uint32_t bn;
  struct dinode dip;
  uint8_t buf[BSIZE];
  uint32_t off = inum / IPB;
  uint32_t ipb_off = inum % IPB;

  memcpy(&dip, disk[INODESTART + off] + ipb_off * sizeof(dip), sizeof(dip));

  uint32_t next_free = BMAPSTART + 1; /* data blocks start after bitmap */

  for (uint32_t i = 0; i < n; i += BSIZE) {
    uint32_t n1 = n - i;
    if (n1 > BSIZE) n1 = BSIZE;

    /* find a free block (very simplified) */
    bn = next_free + dip.size / BSIZE; /* naively allocate sequentially */

    if (dip.size / BSIZE < NDIRECT) {
      dip.addrs[dip.size / BSIZE] = bn;
    }
    dip.size += n1;

    memset(buf, 0, BSIZE);
    memcpy(buf, xp + i, n1);
    wsect(bn, buf);
  }

  winode(inum, &dip);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: mkfs fs.img\n");
    exit(1);
  }

  /* 清零磁盘 */
  memset(disk, 0, sizeof(disk));
  memset(iblock_dirty, 0, sizeof(iblock_dirty));

  /* 构建超级块 */
  struct superblock sb;
  sb.magic     = FSMAGIC;
  sb.size      = FSSIZE;
  sb.nblocks   = FSSIZE - BMAPSTART;
  sb.ninodes   = NINODES;
  sb.nlog      = NLOG;
  sb.logstart  = LOGSTART;
  sb.inodestart = INODESTART;
  sb.bmapstart  = BMAPSTART;

  uint8_t buf[BSIZE];
  memset(buf, 0, BSIZE);
  memcpy(buf, &sb, sizeof(sb));
  wsect(1, buf);

  /* 创建根目录 inode (inum=1) */
  struct dinode root_dinode;
  memset(&root_dinode, 0, sizeof(root_dinode));
  root_dinode.type  = T_DIR;
  root_dinode.nlink = 1;
  root_dinode.size  = 2 * sizeof(struct dirent); /* . and .. */

  /* 分配根目录的数据块 */
  uint32_t root_data_block = BMAPSTART + 1;
  root_dinode.addrs[0] = root_data_block;

  winode(ROOTINO, &root_dinode);

  /* 初始化位图块 */
  memset(buf, 0, BSIZE);
  /* 标记已使用的块 */
  for (uint32_t b = 0; b <= root_data_block; b++) {
    buf[b / 8] |= (1 << (b % 8));
  }
  wsect(BMAPSTART, buf);

  /* 创建根目录内容（. 和 .. 条目）*/
  struct dirent de;
  memset(&de, 0, sizeof(de));

  de.inum = ROOTINO;
  strncpy(de.name, ".", DIRSIZ);
  iappend(ROOTINO, (uint8_t*)&de, sizeof(de));

  memset(&de, 0, sizeof(de));
  de.inum = ROOTINO;
  strncpy(de.name, "..", DIRSIZ);
  iappend(ROOTINO, (uint8_t*)&de, sizeof(de));

  /* 写磁盘镜像 */
  int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    perror("open");
    exit(1);
  }

  for (int i = 0; i < FSSIZE; i++) {
    if (write(fd, disk[i], BSIZE) != BSIZE) {
      perror("write");
      exit(1);
    }
  }
  close(fd);

  printf("mkfs: created %s (%d blocks)\n", argv[1], FSSIZE);
  return 0;
}
