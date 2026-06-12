/* trap.c — 中断与异常分发（Lab4 任务1&3，Lab6 扩展）
 *
 * 本文件是内核的"中控室"。当 sys_trap_vector 把寄存器保存完毕，
 * 就会调用 sys_trap_handler()，由它来判断发生了什么事并分派处理。
 *
 * Lab4 实现：处理时冲中断，每次打印 "Tick!"
 * Lab5 扩展：在时钟中断中增加 yield()，触发进程调度
 * Lab6 扩展：增加 usertrap()，处理来自用户态的 ecall 系统调用
 */

#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "types.h"
#include "proc.h"

void plicinit(void) {
    int hartid = 0;
    *(volatile uint8*)(UART0 + 1) = 0x01;
    *(uint32*)(PLIC_PRIORITY + UART0_IRQ * 4) = 1;
    *(uint32*)PLIC_SENABLE(hartid) |= (1 << UART0_IRQ);
    *(uint32*)PLIC_SPRIORITY(hartid) = 0;
}

#define TickRate 10
uint64 ticks = 0;

/* 声明 sys_trap_vector 汇编入口（在 kernelvec.S 中定义）*/
extern char sys_trap_vector[];

/* 蹦床页符号（trampoline.S + kernel.ld）*/
extern char trampoline[];
extern char uservec[];
extern char userret[];

extern struct proc *myproc(void);

extern void yield(void);
/* ================================================================
 * trapinithart — 设置 S-Mode 陷阱向量
 *
 * 告诉 CPU：当 S-Mode 下发生中断/异常时，跳转到 sys_trap_vector。
 * 在 main.c 的 start_main() 中调用一次即可（每个 CPU 核心调用一次）。
 * ================================================================ */
void trapinithart(void) {
  /* ================================================================
   * TODO [Lab4-任务1]：注册 S-Mode 陷阱向量入口
   *
   * 目标：告诉 CPU，当 S-Mode 下发生中断或异常时，应跳转到哪个地址开始处理。
   *
   * 你需要回答以下问题后，再着手实现：
   *   1. 哪个 CSR 寄存器存放 S-Mode 陷阱处理入口地址？
   *      w_sie(r_sie() | SIE_SSIE);（提示：查阅 kernel/include/riscv.h 中以 w_s 开头的写函数）
   *   2. sys_trap_vector 是汇编中定义的一个地址标签，已在本文件顶部声明为
   *      extern char sys_trap_vector[]。如何从 C 中取得它的地址并转换为 uint64？
   *   3. 陷阱向量寄存器有「直接模式」和「向量模式」两种，本框架使用哪种？
   * ================================================================ */
  w_stvec((uint64)sys_trap_vector);
}

/* ================================================================
 * sys_trap_handler — 内核态中断/异常总处理函数（由 sys_trap_vector 汇编调用）
 *
 * 本函数从 CSR 读取中断原因，然后根据类型分发处理：
 *
 *   scause 最高位（bit 63）：
 *     = 1 → 异步中断（Interrupt），低位表示具体类型
 *     = 0 → 同步异常（Exception），不应在内核中发生
 *
 *   常见中断类型（irq 值）：
 *     1  → 软件中断（由 M-Mode 的 timervec 注入的时钟信号）
 *     5  → S-Mode 时钟中断（如果直接委托到 S-Mode）
 *     9  → 外部中断（UART 键盘输入等）
 * ================================================================ */
void sys_trap_handler(void) {
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if (intr_get())
    panic("sys_trap_handler: entered with interrupts enabled");

  if (scause & 0x8000000000000000L) {
    /* 这是一个异步中断 */
    uint64 irq = scause & 0xff;

    switch (irq) {
    case 1:
      w_sip(r_sip()& ~(1<<1));
      ticks++;
      if(ticks % TickRate == 0){
        printf("Tick! ticks=%d\n", ticks);
      }
      if(myproc() != 0)
        yield();
      break;

    case 9:
    int hartid = 0;
    int irq1 = *(uint32*)PLIC_SCLAIM(hartid);
    if (irq1 != 0) {
      if (irq1 == UART0_IRQ) {
        volatile char c = *(volatile unsigned char*)UART0;
        printf("key:%c\n", c);
      }
      *(uint32*)PLIC_SCLAIM(hartid) = irq1;
    }
    break;

    default:
      printf("sys_trap_handler: unknown interrupt irq=%ld\n", irq);
      break;
    }
  }
  else if (scause == 8) {
    usertrap();
  }
  else {
    /* 同步异常：无法恢复，直接 panic */
    printf("sys_trap_handler: exception! scause=%lx, sepc=%p, stval=%p\n",
       scause, sepc, r_stval());
    panic("sys_trap_handler: unexpected exception");
  }

  /* ================================================================
   * 恢复 sepc 和 sstatus：
   * 某些情况下（如嵌套中断）它们可能被修改过，需要还原。
   * ================================================================ */
  w_sepc(sepc);
  w_sstatus(sstatus);
}


/* ================================================================
 * usertrap — 用户态陷阱处理（Lab6 新增）
 *
 * 当用户程序执行 ecall 时，CPU 切换到 S-Mode 并调用此函数。
 *
 * 区别于 sys_trap_handler：
 *   - 需要切换陷阱向量到 sys_trap_vector（防止用户态 PC 出现在栈跟踪里）
 *   - 需要将 epc 加 4，跳过 ecall 指令（否则返回后又会执行 ecall）
 *   - 只处理 scause == 8（来自 U-Mode 的 ecall）
 * ================================================================ */
void usertrap(void) {
  /* 蹦床 uservec 未保存 sepc，需要在此手动保存 */
  myproc()->trapframe->epc = r_sepc();

  w_stvec((uint64)sys_trap_vector);

  uint64 scause = r_scause();

  if (scause & 0x8000000000000000L) {
    /* 用户态期间发生的异步中断（时钟等）*/
    uint64 irq = scause & 0xff;

    if (irq == 1) {
      w_sip(r_sip() & ~(1<<1));
      ticks++;
      if (ticks % TickRate == 0)
        printf("Tick! ticks=%d\n", ticks);
      if (myproc() != 0)
        yield();
    } else {
      printf("usertrap: unknown interrupt irq=%ld\n", irq);
    }

    usertrapret();
  } else if (scause == 8) {
    /* 来自 U-Mode 的 ecall（系统调用）*/
    myproc()->trapframe->epc += 4;
    intr_on();

    /* 分发给系统调用处理函数 */
    syscall();

    usertrapret();
  } else {
    /* 用户态发生异常，直接终止该进程 */
    printf("usertrap: unexpected scause=%p sepc=%p stval=%p\n",
           scause, r_sepc(), r_stval());
    panic("usertrap");
  }
}

void usertrapret(void)
{
    struct proc *p = myproc();

    intr_off();

    /* 计算蹦床代码中 userret 的虚拟地址 */
    uint64 trampoline_userret = TRAMPOLINE +
        ((uint64)userret - (uint64)trampoline);

    /* 设置下次用户态陷阱入口为蹦床 uservec */
    w_stvec(TRAMPOLINE + ((uint64)uservec - (uint64)trampoline));

    /* 填充陷阱帧：下一次从用户态陷入时 uservec 需要这些信息 */
    p->trapframe->kernel_satp = r_satp();
    p->trapframe->kernel_sp = p->kstack + PGSIZE;
    p->trapframe->kernel_trap = (uint64)usertrap;
    p->trapframe->kernel_hartid = r_tp();

    /* 配置 sstatus：sret 后进入 U-Mode，开中断 */
    uint64 sstatus = r_sstatus();
    sstatus &= ~(1L << 8);  // SPP=0 → U-Mode
    sstatus |= (1L << 5);   // SPIE=1 → 用户态开中断
    w_sstatus(sstatus);

    /* 设置用户返回地址 */
    w_sepc(p->trapframe->epc);

    /* 预设 sscratch 为 TRAPFRAME（蹦床 uservec 的入口需要它）*/
    w_sscratch(TRAPFRAME);

    /* 计算用户页表的 satp 值 */
    uint64 user_satp = MAKE_SATP(p->pagetable);

    /* 通过函数指针跳入蹦床 userret，在蹦床中切换页表并 sret */
    ((void (*)(uint64, uint64))trampoline_userret)(TRAPFRAME, user_satp);
}

