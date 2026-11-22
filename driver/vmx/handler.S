/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vCPU entry and exit handlers
 *
 * Author: Gavin Shan <shan.gavin@gmail.com> 2026
 */
#include <linux/linkage.h>
#include <linux/objtool.h>
#include <asm/nospec-branch.h>

/**
 * __vmx_vcpu_entry - Run a vCPU
 *
 * @vcpu: vCPU instance (struct vmx_vcpu)
 * @flags: Instruction used to kick off the vCPU
 *
 * Returns: 0 on vCPU-exit, 1 on any error
 */
SYM_FUNC_START(__vmx_vcpu_entry)
	/* New stack frame */
	pushq %rbp
	movq  %rsp, %rbp

	/* Save host's non-volatile registers */
        pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %rbx

	/* Save argument (@vCPU) */
	pushq %rdi
	pushq %rsi

	/* Fix up host's stack pointer */
	lea (%rsp), %rsi
	call vmx_vcpu_load_host_rsp

	/* Reload @vcpu and its GPRs */
	movq 8(%rsp), %rdi
	movq (24 +   0)(%rdi), %rax
	movq (24 +   8)(%rdi), %rbx
	movq (24 +  16)(%rdi), %rcx
	movq (24 +  24)(%rdi), %rdx
	movq (24 +  32)(%rdi), %rbp
	movq (24 +  48)(%rdi), %rsi
	movq (24 +  64)(%rdi), %r8
	movq (24 +  72)(%rdi), %r9
	movq (24 +  80)(%rdi), %r10
	movq (24 +  88)(%rdi), %r11
	movq (24 +  96)(%rdi), %r12
	movq (24 + 104)(%rdi), %r13
	movq (24 + 112)(%rdi), %r14
	movq (24 + 120)(%rdi), %r15

        movq (24 +  56)(%rdi), %rdi

	/* VMX_VCPU_ENTRY_VMRESUME */
	testl $1, (%rsp)
	jz .Lvcpu_vmlaunch

	UNWIND_HINT_SAVE

	vmresume
	jmp .Lvcpu_entry_fail

.Lvcpu_vmlaunch:
	vmlaunch
	jmp .Lvcpu_entry_fail

SYM_INNER_LABEL_ALIGN(__vmx_vcpu_exit, SYM_L_GLOBAL)
	UNWIND_HINT_RESTORE
	ENDBR

	/* Temporarily save guest's %rdi */
	pushq %rdi

	/* Reload @vcpu and save its GPRs */
	movq 16(%rsp), %rdi
	movq %rax, (24 +   0)(%rdi)
        movq %rbx, (24 +   8)(%rdi)
        movq %rcx, (24 +  16)(%rdi)
        movq %rdx, (24 +  24)(%rdi)
        movq %rbp, (24 +  32)(%rdi)
        movq %rsi, (24 +  48)(%rdi)
        movq %r8,  (24 +  64)(%rdi)
        movq %r9,  (24 +  72)(%rdi)
        movq %r10, (24 +  80)(%rdi)
        movq %r11, (24 +  88)(%rdi)
        movq %r12, (24 +  96)(%rdi)
        movq %r13, (24 + 104)(%rdi)
        movq %r14, (24 + 112)(%rdi)
        movq %r15, (24 + 120)(%rdi)

	/* Save guest's %rdi */
	popq (24 +  56)(%rdi)

	/* Return 0 to indicate success */
	xor  %ebx, %ebx

.Lhost_restore:
	/* Put return value to %eax */
	movq %rbx, %rax

	/* Pop saved argument (@vcpu and @flags) */
	popq %rbx
	popq %rbx

	/* Restore non-volatile registers */
	popq %rbx
	popq %r12
	popq %r13
	popq %r14
	popq %r15
	popq %rbp

	RET

.Lvcpu_entry_fail:
	mov  $1, %ebx
	jmp  .Lhost_restore
SYM_FUNC_END(__vmx_vcpu_entry)
