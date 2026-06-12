/* trampoline.S — 蹦床汇编（Lab6 补充）
 *
 * 蹦床页同时映射在内核页表和用户页表的 TRAMPOLINE 虚拟地址，
 * 使得页表切换时 CPU 始终能找到正在执行的代码。
 *
 * uservec：用户态 traps → 保存寄存器 → 切内核页表 → 跳 usertrap()
 * userret：usertrapret() 调用 → 切用户页表 → 恢复寄存器 → sret
 */

    .section .text.trampoline, "ax"
    .align 12       /* 页对齐 (2^12 = 4096) */
    .globl trampoline
trampoline:
    .globl uservec

uservec:
    /* 第1步：原子交换 a0 和 sscratch，获取 trapframe 地址 */
    csrrw a0, sscratch, a0

    /* 第2步：保存用户寄存器到 trapframe（按偏移量严格对应）*/
    sd ra, 40(a0)
    sd sp, 48(a0)
    sd gp, 56(a0)
    sd tp, 64(a0)
    sd t0, 72(a0)
    sd t1, 80(a0)
    sd t2, 88(a0)
    sd s0, 96(a0)
    sd s1, 104(a0)
    /* a0 稍后通过 sscratch 保存 */
    sd a1, 120(a0)
    sd a2, 128(a0)
    sd a3, 136(a0)
    sd a4, 144(a0)
    sd a5, 152(a0)
    sd a6, 160(a0)
    sd a7, 168(a0)
    sd s2, 176(a0)
    sd s3, 184(a0)
    sd s4, 192(a0)
    sd s5, 200(a0)
    sd s6, 208(a0)
    sd s7, 216(a0)
    sd s8, 224(a0)
    sd s9, 232(a0)
    sd s10, 240(a0)
    sd s11, 248(a0)
    sd t3, 256(a0)
    sd t4, 264(a0)
    sd t5, 272(a0)
    sd t6, 280(a0)

    /* 第3步：从 sscratch 取回用户原始 a0，保存到 trapframe */
    csrr t0, sscratch
    sd t0, 112(a0)

    /* 第4步：保存用户 sepc（硬件自动放入 sepc）*/
    csrr t0, sepc
    sd t0, 24(a0)

    /* 第5步：从 trapframe 加载内核信息（必须在切页表之前！）*/
    ld t1, 0(a0)        /* kernel_satp */
    ld sp, 8(a0)        /* kernel_sp */
    ld tp, 32(a0)       /* kernel_hartid */
    ld t0, 16(a0)       /* kernel_trap (usertrap 函数地址) */

    /* 第6步：切换到内核页表，跳入 usertrap() */
    csrw satp, t1
    sfence.vma
    jr t0

    .globl userret
userret:
    /* 参数：a0 = TRAPFRAME 虚拟地址, a1 = 用户 satp 值 */

    /* 第1步：切换到用户页表 */
    csrw satp, a1
    sfence.vma

    /* 第2步：从 trapframe 恢复用户寄存器 */
    ld t0, 24(a0)        /* epc */
    csrw sepc, t0

    ld ra, 40(a0)
    ld sp, 48(a0)
    ld gp, 56(a0)
    ld tp, 64(a0)
    ld t0, 72(a0)
    ld t1, 80(a0)
    ld t2, 88(a0)
    ld s0, 96(a0)
    ld s1, 104(a0)
    /* a0 最后恢复（否则丢失基址）*/
    ld a1, 120(a0)
    ld a2, 128(a0)
    ld a3, 136(a0)
    ld a4, 144(a0)
    ld a5, 152(a0)
    ld a6, 160(a0)
    ld a7, 168(a0)
    ld s2, 176(a0)
    ld s3, 184(a0)
    ld s4, 192(a0)
    ld s5, 200(a0)
    ld s6, 208(a0)
    ld s7, 216(a0)
    ld s8, 224(a0)
    ld s9, 232(a0)
    ld s10, 240(a0)
    ld s11, 248(a0)
    ld t3, 256(a0)
    ld t4, 264(a0)
    ld t5, 272(a0)
    ld t6, 280(a0)

    /* 最后恢复 a0 */
    ld a0, 112(a0)

    /* 第3步：返回用户态 */
    sret
