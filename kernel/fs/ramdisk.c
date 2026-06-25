/* ramdisk.c — 内存磁盘驱动（替代 virtio，用于 Lab7 调试）
 *
 * 在内存中模拟一个块设备，不需要真实磁盘。
 * 数据存储在一个静态数组中，读写均在内存中进行。
 * 暴露接口：ramdisk_read(b, write) — 与 virtio_disk_rw 签名一致
 */

#include "defs.h"
#include "fs.h"
#include "types.h"

/* 磁盘镜像大小：1000 块 × 1024 字节 = 1MB */
#define NDISKBLOCKS 1000

/* 全局磁盘缓冲区 */
static uchar disk_data[NDISKBLOCKS][BSIZE];
static int disk_loaded = 0;

extern char _binary_fs_img_start[];
extern char _binary_fs_img_end[];

void ramdisk_init(void) {
  if (disk_loaded) return;

  /* 从链接的内嵌镜像加载数据（如果有的话）*/
  uint64 img_size = (uint64)_binary_fs_img_end - (uint64)_binary_fs_img_start;
  if (img_size > 0 && img_size <= sizeof(disk_data)) {
    memmove(disk_data, _binary_fs_img_start, img_size);
  }
  disk_loaded = 1;
}

void ramdisk_rw(struct buf *b, int write) {
  if (!disk_loaded) ramdisk_init();

  if (b->blockno >= NDISKBLOCKS)
    panic("ramdisk: block number out of range");

  if (write) {
    memmove(disk_data[b->blockno], b->data, BSIZE);
  } else {
    memmove(b->data, disk_data[b->blockno], BSIZE);
  }
}
