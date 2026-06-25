# Makefile — 构建系统
#
# 使用方法：
#   make          # 编译内核
#   make run      # 编译并在 QEMU 中启动内核
#   make debug    # 启动 QEMU 并等待 GDB 连接（端口 1234）
#   make clean    # 清除所有编译产物

# ============================================================
# 工具链配置
# ============================================================
CROSS   = riscv64-unknown-elf-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump
OBJCOPY = $(CROSS)objcopy

# ============================================================
# 编译标志
# ============================================================
CFLAGS = -nostdlib -fno-builtin -mcmodel=medany \
         -march=rv64gc -mabi=lp64d \
         -g -Wall -ffreestanding \
         -I kernel/include

HOSTCC = gcc

SRCS = \
    kernel/boot/entry.S \
    kernel/driver/uart.c \
    kernel/boot/main.c \
    kernel/driver/console.c \
    kernel/mm/kalloc.c \
    kernel/mm/vm.c \
    kernel/boot/start.c \
    kernel/trap/kernelvec.S \
    kernel/trap/trap.c \
    kernel/trap/timervec.S \
    kernel/proc/proc.c \
    kernel/proc/swtch.S \
    kernel/syscall/syscall.c \
    kernel/syscall/sysproc.c \
    kernel/syscall/sysfile.c \
    kernel/trap/trampoline.S \
    kernel/fs/bio.c \
    kernel/fs/fs.c \
    kernel/fs/file.c \
    kernel/fs/ramdisk.c

KERNEL  = kernel.elf
LDSCRIPT = kernel.ld

USER_DIR  = user
USER_SRCS = $(USER_DIR)/usys.S $(USER_DIR)/prozero.c
USER_LD   = $(USER_DIR)/user.ld
USER_ELF  = $(USER_DIR)/prozero.elf
USER_BIN  = $(USER_DIR)/prozero.bin
USER_OBJ  = $(USER_DIR)/prozero.o

# 磁盘镜像
FS_IMG = fs.img
FS_IMG_O = fs_img.o
MKFS   = mkfs

# ============================================================
# 构建目标
# ============================================================
all: $(KERNEL)

# 构建 mkfs 工具（主机编译）
$(MKFS): mkfs.c
	$(HOSTCC) -o $@ $<

# 生成磁盘镜像
$(FS_IMG): $(MKFS)
	./$(MKFS) $@

# 嵌入磁盘镜像为 ELF 对象文件
$(FS_IMG_O): $(FS_IMG)
	$(OBJCOPY) -I binary -O elf64-littleriscv --binary-architecture=riscv $< $@

# 编译用户程序
$(USER_ELF): $(USER_SRCS) $(USER_LD)
	$(CC) $(CFLAGS) -T $(USER_LD) $(USER_SRCS) -o $@

# 提取原始二进制
$(USER_BIN): $(USER_ELF)
	$(OBJCOPY) -O binary $< $@

# 包装为 ELF 对象文件
$(USER_OBJ): $(USER_BIN)
	$(OBJCOPY) -I binary -O elf64-littleriscv --binary-architecture=riscv $< $@

$(KERNEL): $(SRCS) $(LDSCRIPT) $(USER_OBJ) $(FS_IMG_O)
	$(CC) $(CFLAGS) -T $(LDSCRIPT) $(SRCS) $(USER_OBJ) $(FS_IMG_O) -o $@
	@echo "======================================"
	@echo " 内核编译成功：$(KERNEL)"
	@echo " 现在运行 'make run' 启动 QEMU"
	@echo "======================================"

# 在 QEMU 中运行内核
run: $(KERNEL)
	qemu-system-riscv64 \
	    -machine virt \
	    -bios none \
	    -kernel $(KERNEL) \
	    -nographic
	# 退出 QEMU：按 Ctrl+A，然后按 X

# 启动 QEMU 并暂停，等待 GDB 连接（调试模式）
debug: $(KERNEL)
	qemu-system-riscv64 \
	    -machine virt \
	    -bios none \
	    -kernel $(KERNEL) \
	    -nographic \
	    -s -S &
	@echo ""
	@echo "QEMU 已暂停，等待 GDB 连接..."
	@echo "在新终端中运行："
	@echo "  gdb-multiarch $(KERNEL)"
	@echo "  (gdb) target remote :1234"
	@echo "  (gdb) break _entry"
	@echo "  (gdb) continue"
	@echo ""

# 清除编译产物
clean:
	rm -f $(KERNEL) *.o *.d $(USER_DIR)/*.o $(USER_DIR)/*.elf $(USER_DIR)/*.bin
	rm -f $(MKFS) $(FS_IMG) $(FS_IMG_O)

.PHONY: all run debug clean
