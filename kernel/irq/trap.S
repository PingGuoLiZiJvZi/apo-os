#define concat_temp(x, y) x ## y
#define concat(x, y) concat_temp(x, y)
#define MAP(c, f) c(f)

#define LOAD  ld
#define STORE sd
#define XLEN  8

#define REGS_LO16(f) \
      f( 1)       f( 3) f( 4) f( 5) f( 6) f( 7) f( 8) f( 9) \
f(10) f(11) f(12) f(13) f(14) f(15)

#define REGS_HI16(f) \
                                    f(16) f(17) f(18) f(19) \
f(20) f(21) f(22) f(23) f(24) f(25) f(26) f(27) f(28) f(29) \
f(30) f(31)
#define NR_REGS 32

#define REGS(f) REGS_LO16(f) REGS_HI16(f)

#define PUSH(n) STORE concat(x, n), (n * XLEN)(sp);
#define POP(n)  LOAD  concat(x, n), (n * XLEN)(sp);

#define CONTEXT_SIZE  ((NR_REGS + 5) * XLEN)
#define OFFSET_SP     ( 2 * XLEN)
#define OFFSET_CAUSE  ((NR_REGS + 0) * XLEN)
#define OFFSET_STATUS ((NR_REGS + 1) * XLEN)
#define OFFSET_EPC    ((NR_REGS + 2) * XLEN)
#define OFFSET_PDIR   ((NR_REGS + 3) * XLEN)
#define OFFSET_NP     ((NR_REGS + 4) * XLEN)
#define KERNEL 0
#define USER   1

.align 3
.globl trap
trap:
  csrrw sp, sscratch, sp   # (1) atomically exchange sp and sscratch
  bnez sp, save_context    # (2) take the branch if we trapped from user
  csrr sp, sscratch        # (3) if we trapped from kernel, restore the original sp

save_context:
  addi sp, sp, -CONTEXT_SIZE

  MAP(REGS, PUSH)

  # Save satp into OFFSET_PDIR
  csrr t0, satp
  STORE t0, OFFSET_PDIR(sp)

  # Detect privilege level (NP):
  # If sscratch == sp + CONTEXT_SIZE, we trapped from kernel (sscratch was 0,
  # so we restored original sp which equals sp + CONTEXT_SIZE after the addi).
  # Otherwise we trapped from user.
  csrr t0, sscratch
  addi t1, sp, CONTEXT_SIZE
  beq t0, t1, kernel_trap
  li t0, USER
  STORE t0, OFFSET_NP(sp)
  j np_end

kernel_trap:
  li t0, KERNEL
  STORE t0, OFFSET_NP(sp)

np_end:
  csrr t0, sscratch
  STORE t0, OFFSET_SP(sp)
  csrw sscratch, x0

  csrr t0, scause
  csrr t1, sstatus
  csrr t2, sepc

  STORE t0, OFFSET_CAUSE(sp)
  STORE t1, OFFSET_STATUS(sp)
  STORE t2, OFFSET_EPC(sp)

  mv a0, sp
  call trap_handle
  mv sp, a0

  # Restore satp from OFFSET_PDIR (context switch may have changed it)
  LOAD t0, OFFSET_PDIR(sp)
  csrw satp, t0
  sfence.vma zero, zero

  LOAD t1, OFFSET_STATUS(sp)
  LOAD t2, OFFSET_EPC(sp)
  csrw sstatus, t1
  csrw sepc, t2

  LOAD t3, OFFSET_NP(sp)
  beqz t3, kernel_restore
  addi t0, sp, CONTEXT_SIZE
  csrw sscratch, t0
  MAP(REGS, POP)
  LOAD sp, OFFSET_SP(sp)
  sret

kernel_restore:
  MAP(REGS, POP)
  addi sp, sp, CONTEXT_SIZE
  sret
  
# Direct context restore for bootstrap: restores satp, sstatus, sepc,
# all GPRs, and srets into the target context.  Never returns.
.align 3
.globl asm_switch
asm_switch:
  # a0 = pointer to Context

  # Restore satp
  LOAD t0, OFFSET_PDIR(a0)
  csrw satp, t0
  sfence.vma zero, zero

  # Restore sstatus and sepc
  LOAD t1, OFFSET_STATUS(a0)
  LOAD t2, OFFSET_EPC(a0)
  csrw sstatus, t1
  csrw sepc, t2

  # Check NP: if kernel, sscratch = 0; if user, sscratch = sp + CONTEXT_SIZE
  LOAD t3, OFFSET_NP(a0)
  beqz t3, 1f
  addi t0, a0, CONTEXT_SIZE
  csrw sscratch, t0
  mv sp, a0
  MAP(REGS, POP)
  LOAD sp, OFFSET_SP(sp)
  sret

1:
  csrw sscratch, zero
  mv sp, a0
  MAP(REGS, POP)
  addi sp, sp, CONTEXT_SIZE
  sret
