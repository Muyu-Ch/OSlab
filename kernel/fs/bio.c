/* bio.c — 块设备缓冲层（Lab7 任务1）*/

#include "defs.h"
#include "fs.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "types.h"

/* 缓冲池（全局，内核中只有一个）*/
struct {
  struct buf buf[NBUF];
  struct buf head;
} bcache;

/* 外部磁盘驱动函数 */
extern void ramdisk_rw(struct buf *b, int write);

/* ================================================================
 * binit — 初始化缓冲池（内核启动时调用）
 * ================================================================ */
void binit(void) {
  struct buf *b;

  /* 初始化哨兵头节点 */
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;

  /* 将每个 buf 头插到链表 */
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

/* ================================================================
 * bget — 在缓冲池中找到指定 (dev, blockno) 的缓冲块
 * ================================================================ */
static struct buf *bget(uint dev, uint blockno) {
  struct buf *b;

  /* 步骤1：查找是否已缓存 */
  for (b = bcache.head.next; b != &bcache.head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      return b;
    }
  }

  /* 步骤2：LRU 淘汰——从链表尾部找空闲块 */
  for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      return b;
    }
  }
  panic("bget: no buffers");
}

/* ================================================================
 * bread — 读取磁盘块
 * ================================================================ */
struct buf *bread(uint dev, uint blockno) {
  struct buf *b;

  b = bget(dev, blockno);

  if (!b->valid) {
    ramdisk_rw(b, 0);
    b->valid = 1;
  }

  return b;
}

/* ================================================================
 * bwrite — 将缓冲块的修改写回磁盘
 * ================================================================ */
void bwrite(struct buf *b) {
  ramdisk_rw(b, 1);
}

/* ================================================================
 * brelse — 释放对缓冲块的占用
 * ================================================================ */
void brelse(struct buf *b) {
  b->refcnt--;
  if (b->refcnt == 0) {
    /* LRU：从当前位置摘除，头插到哨兵后面 */
    b->next->prev = b->prev;
    b->prev->next = b->next;

    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}
