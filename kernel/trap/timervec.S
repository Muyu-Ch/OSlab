    .globl timervec
    .align 4
timervec:
# 1. 交换 a0 和 mscratch，让 a0 指向 timer_scratch 数组
    csrrw  a0, mscratch, a0
# 暂存 a1, a2, a3 到 scratch 前三个槽位
    sd     a1, 0(a0)
    sd     a2, 8(a0)
    sd     a3, 16(a0)

# 2. 安排下一次时钟中断：mtimecmp += interval
    ld     a1, 24(a0)       # a1 = MTIMECMP 物理地址
    ld     a2, 32(a0)       # a2 = interval 间隔
    ld     a3, 0(a1)        # a3 = 当前 MTIMECMP 值
    add    a3, a3, a2
    sd     a3, 0(a1)        # 更新 MTIMECMP

# 3. 向 S-Mode 注入软件中断信号
    li     a1, 2
    csrw   sip, a1

# 4. 恢复 a3, a2, a1
    ld     a3, 16(a0)
    ld     a2, 8(a0)
    ld     a1, 0(a0)
# 换回 a0 原始值
    csrrw  a0, mscratch, a0

# 5. 从 M-Mode 陷阱返回
    mret