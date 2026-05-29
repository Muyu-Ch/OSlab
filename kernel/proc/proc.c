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

// 用户态 proczero：两次 ecall + 死循环
static uint8 proczero_code[] = {
    0x73, 0x00, 0x00, 0x00, // ecall
    0x73, 0x00, 0x00, 0x00, // ecall
    0x6f, 0x00, 0x00, 0x00  // j . 死循环
};


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
  
  p->pagetable = (pagetable_t)kalloc();
  if (p->pagetable == 0) panic("userinit: pagetable kalloc failed");
  memset(p->pagetable, 0, PGSIZE);
  uvminit(p->pagetable);

  uint64 code_pa=(uint64)kalloc();// 为用户程序代码分配一个物理页面
  if(code_pa == 0){panic("userinit: kalloc failed");}
  
  memmove(
    (void*)code_pa,
    proczero_code,
    sizeof(proczero_code)
  );

  mappages(
    p->pagetable,
    0,
    PGSIZE,
    code_pa,
    PTE_R | PTE_X | PTE_U
  );

  uint64 stack_pa=(uint64)kalloc();
  if(stack_pa == 0){panic("userinit: kalloc failed");}

  mappages(
    p->pagetable,
    PGSIZE,
    PGSIZE,
    stack_pa,
    PTE_R | PTE_W | PTE_U
  );
  
  memset(
    p->trapframe,
    0,
    sizeof(struct trapframe)
  );

  p->trapframe->kernel_satp = MAKE_SATP(kernel_pagetable);
  p->trapframe->epc = 0;
  p->trapframe->sp = PGSIZE; // 用户栈顶地址（虚拟地址 PGSIZE）
  
  strncpy(
    p->name, 
    "proczero", 
    sizeof(p->name)
  );//设置进程名称（调试用）
  
  p->status = TASK_READY;
}