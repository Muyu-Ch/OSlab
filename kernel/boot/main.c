/* main.c — 内核入口 */

#include "../include/defs.h"

/* ============================================================
 * 基础初始化
 * ============================================================ */
void Lab3(void) {
  kinit(); kvmininit(); kvminithart();
  printf("Paging enabled! Interrupts on.\n");
}

void Lab4(void) {
  trapinithart();
  plicinit();
  intr_on();
}

void Lab5(void) {
  procinit();
  userinit();
  scheduler();
}

void Lab6(void) {
  procinit();
  userinit();
  scheduler();
}

void Lab7(void) {
  fsinit(1);
  fileinit();
  procinit();
  userinit();
  scheduler();
}

void start_main(void) {
  printf("main start!\n");

  Lab3();
  Lab4();

  Lab7();   /* 始终用 Lab7（fsinit 无副作用），切换 prozero.c 即可 */

  while (1);
}
