/* proc.c — 进程管理（Lab5 任务1、3、4）
 *
 * 本文件实现了进程生命周期管理的核心逻辑：
 *   - procinit()   : 初始化进程表
 *   - allocproc()  : 为新进程分配 PCB
 *   - scheduler()  : 调度器主循环（无限轮询、找到就绪进程就运行）
 *   - yield()      : 当前进程主动放弃 CPU（配合时钟中断使用）
 */

#include "proc.h"
#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "types.h"

extern char sys_trap_vector[];

// 用户程序二进制，由 Makefile 从 user/prozero.c + usys.S 自动编译生成
extern char _binary_user_prozero_bin_start[];
extern char _binary_user_prozero_bin_end[];


/* 全局进程表和 CPU 描述符（在 proc.h 中 extern 声明）*/
struct proc proc[NPROC];
struct cpu cpus[NCPU];



/* 进程 ID 计数器（每次 allocpid 返回后递增）*/
static int nextpid = 1;

extern void forkret(void);
/* ================================================================
 * mycpu — 获取当前 CPU 核心的 cpu 结构指针
 *
 * 实现方式：读取 tp 寄存器（在 start.c 中被设置为 hartid）
 * ================================================================ */
struct cpu *mycpu(void) {
  int hartid = r_tp();
  return &cpus[hartid];
}

/* ================================================================
 * myproc — 获取当前 CPU 上正在运行的进程的 PCB 指针
 * ================================================================ */
struct proc *myproc(void) { return mycpu()->proc; }

/* ================================================================
 * allocpid — 分配一个唯一的进程 ID
 * ================================================================ */
int allocpid(void) { return nextpid++; }

/* ================================================================
 * procinit — 初始化进程表（内核启动时调用一次）
 *
 * 任务：将进程表中所有条目的状态初始化为 TASK_FREE。
 * ================================================================ */
void procinit(void) {
  /* ================================================================
   * TODO [Lab5-任务1-步骤1]：
   *   遍历 proc[] 数组，将每个进程的 status 置为 TASK_FREE。
   * ================================================================ */
  for (int i = 0; i < NPROC; i++) {
    proc[i].status = TASK_FREE;
  }
}

/* ================================================================
 * allocproc — 在进程表中找一个空槽并初始化
 *
 * 返回：指向已初始化的 PCB 的指针；若进程表满，返回 0。
 *
 * 初始化内容：
 *   - 分配 pid
 *   - 将状态从 TASK_FREE 改为 TASK_ALLOCATED
 *   - 分配 trapframe 页（用于保存用户寄存器）
 *   - 初始化内核 context（ra 设为某个"进程首次被调度时跳入的地址"）
 * ================================================================ */
struct proc *allocproc(void) {
  struct proc *p;

  /* 在进程表中寻找一个 TASK_FREE 的槽位 */
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->status == TASK_FREE)
      goto found;
  }
  return 0; /* 进程表已满 */

found:
  /* ================================================================
   * TODO [Lab5-任务1-步骤2]：
   *   完成进程初始化：
   *   1. 分配 pid：调用 allocpid()
   *   2. 分配 trapframe 页：调用 kalloc()；若失败则将状态恢复为 TASK_FREE 并返回0
   *   3. 将进程状态设为 TASK_ALLOCATED
   * ================================================================ */
  p->pid = allocpid();
  p->trapframe = (struct trapframe *)kalloc();
  if (p->trapframe == 0) {
    p->status = TASK_FREE;
    return 0;
  }
  p->status = TASK_ALLOCATED;
  p->context.ra = (uint64)forkret;
  return p;
}

/* ================================================================
 * scheduler — 调度器主循环（永不返回！）
 *
 * 这是操作系统的"上帝"：它在所有进程之间无限轮转，
 * 当看到一个 TASK_READY 的进程时，就把 CPU 交给它。
 *
 * 流程：
 *   for 每次循环:
 *     1. 打开全局中断（防止系统无法接收时钟信号而死锁）
 *     2. 遍历进程表，找到 TASK_READY 的进程
 *     3. 将该进程标记为 TASK_RUNNING
 *     4. 调用 swtch，从调度器上下文切换到进程的内核上下文
 *     5. 当进程放弃 CPU（yield/sleep/exit）后，swtch 返回到这里
 *     6. 清除 mycpu()->proc，继续找下一个
 * ================================================================ */
void scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  w_stvec((uint64)sys_trap_vector);

  for (;;) {
    /* 必须打开中断！否则时钟信号无法到达，调度无法触发 */
    intr_on();
    for (p = proc; p < &proc[NPROC]; p++) {
      /* ================================================================
       * TODO [Lab5-任务3]：
       *   完成调度器核心逻辑：
       *   1. 检查 p->status == TASK_READY
       *   2. 将状态改为 TASK_RUNNING
       *   3. 将 c->proc 设为 p
       *   4. 调用 swtch 切换到 p 的上下文：swtch(&c->context, &p->context)
       *   5. swtch 返回后（进程放弃了CPU），清零 c->proc
       * ================================================================ */
      if(p->status == TASK_READY) {
        p->status = TASK_RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
      }
    }
  }
}

/* ================================================================
 * yield — 当前进程主动放弃 CPU（由时钟中断处理函数调用）
 *
 * 过程：将自己的状态从 TASK_RUNNING 改回 TASK_READY，然后切回调度器。
 * ================================================================ */
void yield(void) {
  struct proc *p = myproc();
  if (p == 0) return;
  p->status = TASK_READY;
  swtch(&p->context, &mycpu()->context);
}


void forkret(void) {
  usertrapret();
}

void* memset(void* dst, int c, uint64 n)
{
    uint8 *d = (uint8*)dst;
    while (n--)
        *d++ = c;
    return dst;
}

void* memmove(void *dst, const void *src, uint64 n)
{
    uint64 *d = dst;
    const uint64 *s = src;

    if (d < s) {
        // 从前往后拷贝
        while (n--)
            *d++ = *s++;
    } else {
        // 从后往前拷贝（处理重叠情况）
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }

    return dst;
}

char* strncpy(char *dst, const char *src, uint64 n)
{
    uint64 i;
    for (i = 0; i < n && src[i] != 0; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}


void userinit(void){
  struct proc * p = allocproc();// 在进程表中为第一个用户进程分配一个槽位并初始化 PCB
  if(p == 0){panic("userinit: allocproc failed");}

  p->kstack=(uint64)kalloc();// 为该进程分配一个内核栈页面，并设置PCB中的内核栈顶地址字段
  if(p->kstack == 0){panic("userinit: kalloc failed");}
  p->context.sp = p->kstack + PGSIZE;// 设置内核栈指针（栈顶地址）

  /* 创建独立用户页表（含蹦床和陷阱帧映射）*/
  p->pagetable = uvmcreate((uint64)p->trapframe);
  if (p->pagetable == 0) panic("userinit: uvmcreate failed");

  /* 加载用户代码到 VA 0 */
  uint64 code_size = (uint64)_binary_user_prozero_bin_end
                    - (uint64)_binary_user_prozero_bin_start;
  uvminit(p->pagetable, (uint8 *)_binary_user_prozero_bin_start, code_size);

  /* 映射用户栈页（VA PGSIZE ~ 2*PGSIZE）*/
  uint64 stack_pa = (uint64)kalloc();
  if (stack_pa == 0) panic("userinit: stack kalloc failed");
  mappages(p->pagetable, PGSIZE, PGSIZE, stack_pa,
           PTE_R | PTE_W | PTE_U);

  memset(
    p->trapframe,
    0,
    sizeof(struct trapframe)
  );

  p->trapframe->kernel_satp = MAKE_SATP(kernel_pagetable);
  p->trapframe->epc = 0;
  p->trapframe->sp = PGSIZE * 2; // 用户栈顶地址

  strncpy(
    p->name,
    "proczero",
    sizeof(p->name)
  );//设置进程名称（调试用）

  p->sz = PGSIZE;           /* 地址空间大小（代码页，栈手动映射）*/
  p->parent = p;            /* init 进程的父进程是自己 */
  p->status = TASK_READY;
}

/* ================================================================
 * sleep — 让当前进程在 chan 上进入睡眠
 * ================================================================ */
void sleep(void *chan) {
  struct proc *p = myproc();
  p->chan = chan;
  p->status = TASK_SLEEPING;
  swtch(&p->context, &mycpu()->context);
  p->chan = 0;
}

/* ================================================================
 * wakeup — 唤醒所有在 chan 上睡眠的进程
 * ================================================================ */
void wakeup(void *chan) {
  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    if (p->chan == chan && p->status == TASK_SLEEPING) {
      p->status = TASK_READY;
    }
  }
}

/* ================================================================
 * fork — 复制当前进程，创建子进程
 * ================================================================ */
int fork(void) {
  struct proc *np;
  struct proc *p = myproc();

  intr_off();
  if ((np = allocproc()) == 0) { intr_on(); return -1; }
    return -1;

  /* 创建子进程用户页表（含蹦床和陷阱帧）*/
  if ((np->pagetable = uvmcreate((uint64)np->trapframe)) == 0) {
    np->status = TASK_FREE;
    intr_on();
    return -1;
  }

  /* 复制代码页 */
  { uint64 mem = (uint64)kalloc();
    if (mem == 0) goto bad;
    uint64 sz = (uint64)_binary_user_prozero_bin_end
              - (uint64)_binary_user_prozero_bin_start;
    memmove((void *)mem, _binary_user_prozero_bin_start, sz);
    mappages(np->pagetable, 0, PGSIZE, mem,
             PTE_R | PTE_W | PTE_X | PTE_U);
  }

  /* 分配子进程栈页 */
  { uint64 mem = (uint64)kalloc();
    if (mem == 0) goto bad;
    memset((void *)mem, 0, PGSIZE);
    mappages(np->pagetable, PGSIZE, PGSIZE, mem,
             PTE_R | PTE_W | PTE_U);
  }
  np->sz = 2 * PGSIZE;

  /* 复制陷阱帧 */
  memmove(np->trapframe, p->trapframe, PGSIZE);
  np->trapframe->a0 = 0;

  /* 分配内核栈 */
  if ((np->kstack = (uint64)kalloc()) == 0) goto bad;
  np->context.sp = np->kstack + PGSIZE;
  np->context.ra = (uint64)forkret;

  /* 设置父子关系 */
  np->parent = p;
  strncpy(np->name, p->name, sizeof(np->name));
  np->status = TASK_READY;

  intr_on();
  return np->pid;

bad:
  if (np->pagetable)
    kfree((void *)np->pagetable);
  np->status = TASK_FREE;
  intr_on();
  return -1;
}

/* ================================================================
 * exit — 终止当前进程
 * ================================================================ */
void exit(int status) {
  struct proc *p = myproc();

  /* 将所有子进程移交给 init 进程（pid=1）*/
  for (struct proc *pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = &proc[0];  /* proc[0] 是 init */
    }
  }

  /* 唤醒父进程（如果它在 wait 中睡眠）*/
  wakeup(p->parent);

  /* 记录退出状态 */
  p->xstate = status;
  p->status = TASK_ZOMBIE;

  /* 切回调度器，永不返回 */
  swtch(&p->context, &mycpu()->context);
  panic("exit: should never reach here");
}

/* ================================================================
 * wait — 等待子进程退出并回收
 * ================================================================ */
int wait(uint64 addr) {
  struct proc *p = myproc();
  struct proc *np;
  int havekids, pid;

  for (;;) {
    havekids = 0;

    for (np = proc; np < &proc[NPROC]; np++) {
      if (np->parent == p) {
        havekids = 1;

        if (np->status == TASK_ZOMBIE) {
          /* 找到一个僵尸子进程，回收它 */
          pid = np->pid;
          int xstate = np->xstate;

          /* 将退出状态写回用户空间 */
          if (addr != 0 && pid > 0) {
            struct proc *cur = myproc();
            uint64 pa = walkaddr(cur->pagetable, addr);
            if (pa != 0)
              *(int *)pa = xstate;
          }

          /* 释放子进程资源 */
          kfree((void *)np->trapframe);
          kfree((void *)np->kstack);
          kfree((void *)np->pagetable);
          np->pagetable = 0;
          np->kstack = 0;
          np->status = TASK_FREE;

          return pid;
        }
      }
    }

    if (!havekids)
      return -1;

    /* 没有僵尸子进程但有存活子进程，睡眠等待 */
    sleep(p);
  }
}