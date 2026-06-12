/* sysproc.c — 系统调用内核实现（Lab6 任务4）
 *
 * 每个 sys_xxx() 函数是对应系统调用的真正内核实现。
 * 它们不接受参数（参数通过陷阱帧的寄存器传入，用 argint/argaddr 读取），
 * 返回 uint64 类型的结果值。
 */

#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "riscv.h"
#include "types.h"

/* ================================================================
 * sys_getpid — 返回当前进程的 PID
 *
 * 对应的用户接口：int getpid(void)
 *
 * 实现很简单：调用 myproc() 获取当前进程的 PCB，
 * 然后返回它的 pid 字段。
 * ================================================================ */
uint64 sys_getpid(void) {
  /* ================================================================
   * TODO [Lab6-任务4-步骤1]：
   *   调用 myproc() 获取当前进程的 PCB 指针，返回其 pid 字段。
   * ================================================================ */
  return myproc()->pid;
}

/* ================================================================
 * sys_exit — 终止当前进程
 * ================================================================ */
uint64 sys_exit(void) {
  int n;
  argint(0, &n);
  exit(n);
  return 0;  /* never reached */
}

/* ================================================================
 * sys_fork — 创建子进程
 * ================================================================ */
uint64 sys_fork(void) {
  return fork();
}

/* ================================================================
 * sys_wait — 等待子进程退出
 * ================================================================ */
uint64 sys_wait(void) {
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

/* ================================================================
 * sys_write — 向文件描述符写数据
 *
 * 用户接口：int write(int fd, const void *buf, int count)
 * 参数从陷阱帧读取：
 *   fd    = trapframe->a0
 *   buf   = trapframe->a1（用户虚拟地址，不能直接在内核读！）
 *   count = trapframe->a2
 *
 * 简化版：如果 fd==1（标准输出），直接把字符打印到串口。
 * ================================================================ */
uint64 sys_write(void) {
  /* ================================================================
   * TODO [Lab6-任务4-步骤3（进阶）]：
   *   实现简化版 sys_write：
   *   1. int fd = myproc()->trapframe->a0;
   *   2. 获取出参 n（系统调用的第一个参数，可用 argint 拿取），并赋给 p->xstate
   *   3. 打印类似 "Process [pid] exited with code [n]\n"
   *   4. 设置 p->status = TASK_ZOMBIE
   *   5. 调用 swtch 切回调度器：swtch(&p->context, &mycpu()->context);
   * ================================================================ */
  struct proc *p = myproc();

  int fd    = p->trapframe->a0;        // 用户传的 fd
  char *buf = (char *)p->trapframe->a1; // 用户传的 buf
  int count = p->trapframe->a2;         // 用户传的 count

  if (fd == 1) {
    for (int i = 0; i < count; i++) {
      uint64 pa = walkaddr(p->pagetable, (uint64)buf + i);
      if (pa == 0)
        return -1;
      uart_putc(*(char*)pa);
    }
    return count;
  }
  return -1;

}
