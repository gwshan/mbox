// SPDX-License-Identifier: GPL-2.0+
/*
 * vCPU (Virtual CPU) management
 *
 * Author: Gavin Shan <shan.gavin@gmail.com> 2026
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/printk.h>
#include <linux/sched.h>

#include <asm/cpuid/api.h>
#include <asm/msr.h>
#include <asm/vmx.h>

#include "vmx.h"

static inline unsigned long vmcs_get_field(unsigned long field)
{
	unsigned long val;

	asm volatile ("vmread %[field], %[output]"
			: [output] "=r" (val)
			: [field] "r" (field)
			: "cc", "memory");
	return val;
}

static inline void vmcs_set_field(unsigned long field, unsigned long val)
{
	unsigned char retval = 0;

	asm volatile ("vmwrite %[input], %[field]; setna %[ret]"
			: [ret] "=rm" (retval)
			: [field] "r" (field), [input] "r" (val)
			: "cc", "memory");
	if (retval) {
		pr_warn("%s: Error 0x%02x writting 0x%lx to field 0x%lx\n",
			__func__, retval, val, field);
	}
}

/**
 * vmx_vcpu_init - Initialize vCPU instance
 *
 * @vm: virtual machine instance
 * @vcpu: vCPU instance to be initialized
 */
int vmx_vcpu_init(struct vmx_vm *vm, struct vmx_vcpu *vcpu)
{
	struct vmcs *vmcs;
	struct page *page;
	int order;

	/* Alloc vmcs */
	vcpu->vmcs_pages = 1;
	order = get_order(vcpu->vmcs_pages << PAGE_SHIFT);
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!page) {
		pr_warn("%s: Unable to alloc vmcs\n", __func__);
		return -ENOMEM;
	}

	vcpu->vmcs = (unsigned long)page_address(page);
	vmcs = (struct vmcs *)(vcpu->vmcs);
	vmcs->hdr.revision_id = vmx_basic_vmcs_revision_id(vm->config.basic);

	/* Link vCPU to vm */
	vcpu->vm = vm;
	vm->vcpu = vcpu;

	return 0;
}

static void vmx_vcpu_load_guest_area(struct vmx_vcpu *vcpu)
{
	/* Control Registers (apply VMX_CR0/CR4_FIXED MSRs as well) */
	vmcs_set_field(GUEST_CR0,             0x00000020); // NE=1 (PE=0, PG=0 requires Unrestricted Guest)
	vmcs_set_field(GUEST_CR3,             0x00000000);
	vmcs_set_field(GUEST_CR4,             0x00002000); // VMXE=1
	vmcs_set_field(GUEST_DR7,             0x00000400);

	/* RIP & RFLAGS */
	vmcs_set_field(GUEST_RIP,             0x00000000);
	vmcs_set_field(GUEST_RSP,             0x00000000);
	vmcs_set_field(GUEST_RFLAGS,          0x00000002); // Bit 1 must always be 1

	/* Code Segment (16-bit Real Mode, Ring 0, Execute/Read, Accessed) */
	vmcs_set_field(GUEST_CS_SELECTOR,     0x0000);
	vmcs_set_field(GUEST_CS_BASE,         0x00000000);
	vmcs_set_field(GUEST_CS_LIMIT,        0x0000FFFF);
	vmcs_set_field(GUEST_CS_AR_BYTES,     0x0000009B);

	/*
	 * DS/SS/ES/FS/GS segments:
	 *
	 * Selectors = 0x0000, Bases = 0x00000000,
	 * Limits = 0x0000FFFF, AR = 0x00000093
	 */
	vmcs_set_field(GUEST_DS_SELECTOR,     0x0000);
	vmcs_set_field(GUEST_DS_BASE,         0x00000000);
	vmcs_set_field(GUEST_DS_LIMIT,        0x0000FFFF);
	vmcs_set_field(GUEST_DS_AR_BYTES,     0x00000093);

	vmcs_set_field(GUEST_SS_SELECTOR,     0x0000);
	vmcs_set_field(GUEST_SS_BASE,         0x00000000);
	vmcs_set_field(GUEST_SS_LIMIT,        0x0000FFFF);
	vmcs_set_field(GUEST_SS_AR_BYTES,     0x00000093);

	vmcs_set_field(GUEST_ES_SELECTOR,     0x0000);
	vmcs_set_field(GUEST_ES_BASE,         0x00000000);
	vmcs_set_field(GUEST_ES_LIMIT,        0x0000FFFF);
	vmcs_set_field(GUEST_ES_AR_BYTES,     0x00000093);

	vmcs_set_field(GUEST_FS_SELECTOR,     0x0000);
	vmcs_set_field(GUEST_FS_BASE,         0x00000000);
	vmcs_set_field(GUEST_FS_LIMIT,        0x0000FFFF);
	vmcs_set_field(GUEST_FS_AR_BYTES,     0x00000093);

	vmcs_set_field(GUEST_GS_SELECTOR,     0x0000);
	vmcs_set_field(GUEST_GS_BASE,         0x00000000);
	vmcs_set_field(GUEST_GS_LIMIT,        0x0000FFFF);
	vmcs_set_field(GUEST_GS_AR_BYTES,     0x00000093);

	/* LDTR (Unusable) */
	vmcs_set_field(GUEST_LDTR_SELECTOR,   0x0000);
	vmcs_set_field(GUEST_LDTR_BASE,       0x00000000);
	vmcs_set_field(GUEST_LDTR_LIMIT,      0x00000000);
	vmcs_set_field(GUEST_LDTR_AR_BYTES,   0x00010000);

	/* TR (Usable, 32-bit Busy TSS) */
	vmcs_set_field(GUEST_TR_SELECTOR,     0x0000);
	vmcs_set_field(GUEST_TR_BASE,         0x00008000);
	vmcs_set_field(GUEST_TR_LIMIT,        0x000000FF);
	vmcs_set_field(GUEST_TR_AR_BYTES,     0x0000008B);

	/* Descriptor Tables & Non-Register State */
	vmcs_set_field(GUEST_GDTR_BASE,       0x00000000);
	vmcs_set_field(GUEST_GDTR_LIMIT,      0x0000FFFF);
	vmcs_set_field(GUEST_IDTR_BASE,       0x00000000);
	vmcs_set_field(GUEST_IDTR_LIMIT,      0x0000FFFF);
	vmcs_set_field(GUEST_ACTIVITY_STATE,  0x00000000);
	vmcs_set_field(GUEST_INTERRUPTIBILITY_INFO, 0x00000000);
	vmcs_set_field(GUEST_PENDING_DBG_EXCEPTIONS, 0x00000000);
	vmcs_set_field(GUEST_SYSENTER_CS,     0x00000000);
	vmcs_set_field(GUEST_SYSENTER_ESP,    0x00000000);
	vmcs_set_field(GUEST_SYSENTER_EIP,    0x00000000);
	vmcs_set_field(GUEST_IA32_DEBUGCTL,   0x00000000);
	vmcs_set_field(VMCS_LINK_POINTER,     0xFFFFFFFFFFFFFFFF);
}

static void vmx_vcpu_load_host_area(struct vmx_vcpu *vcpu)
{
	struct vmx_vm *vm = vcpu->vm;
	struct vmx_config *config = &vm->config;
	unsigned char xdtr[10];
	unsigned long host_tr_selector;
	unsigned long host_gdt_base;
	unsigned long host_tr_desc;
	unsigned long val;

	asm volatile ("movq %%es, %0\n\t" : "=a" (val) :);
	vmcs_set_field(HOST_ES_SELECTOR, val & 0xF8);
	asm volatile ("movq %%cs, %0\n\t" : "=a" (val) :);
	vmcs_set_field(HOST_CS_SELECTOR, val & 0xF8);
	asm volatile ("movq %%ss, %0\n\t" : "=a" (val) :);
	vmcs_set_field(HOST_SS_SELECTOR, val & 0xF8);
	asm volatile ("movq %%ds, %0\n\t" : "=a" (val) :);
	vmcs_set_field(HOST_DS_SELECTOR, val & 0xF8);
	asm volatile ("movq %%fs, %0\n\t" : "=a" (val) :);
	vmcs_set_field(HOST_FS_SELECTOR, val & 0xF8);
	asm volatile ("movq %%gs, %0\n\t" : "=a" (val) :);
	vmcs_set_field(HOST_GS_SELECTOR, val & 0xF8);
	asm volatile ("str %0\n\t" : "=a" (val) :);
	vmcs_set_field(HOST_TR_SELECTOR, val & 0xFFF8);

	rdmsrq_safe(MSR_IA32_CR_PAT, (u64 *)&val);
	vmcs_set_field(HOST_IA32_PAT, val);
	rdmsrq_safe(HOST_IA32_EFER, (u64 *)&val);
	vmcs_set_field(HOST_IA32_EFER, val);
	rdmsrq_safe(HOST_IA32_PERF_GLOBAL_CTRL, (u64 *)&val);
	vmcs_set_field(HOST_IA32_PERF_GLOBAL_CTRL, val);
	rdmsrq_safe(HOST_IA32_SYSENTER_CS, (u64 *)&val);
	vmcs_set_field(HOST_IA32_SYSENTER_CS, val);

	asm volatile ("movq %%cr0, %0" : "=a" (val) :);
	val |= config->host_cr0_fixed0;
	val &= config->host_cr0_fixed1;
	vmcs_set_field(HOST_CR0,       val);
	asm volatile ("movq %%cr3, %0" : "=a" (val) :);
	vmcs_set_field(HOST_CR3,       val);
	asm volatile ("movq %%cr4, %0" : "=a" (val) :);
	val |= config->host_cr4_fixed0;
	val &= config->host_cr4_fixed1;
	vmcs_set_field(HOST_CR4,       val);

	rdmsrq_safe(HOST_FS_BASE, (u64 *)&val);
	vmcs_set_field(HOST_FS_BASE, val);
	rdmsrq_safe(MSR_GS_BASE, (u64 *)&val);
	vmcs_set_field(HOST_GS_BASE, val);

	asm volatile ("str %0" : "=a" (host_tr_selector) :);
	host_tr_selector &= 0xFFF8;
	asm volatile ("sgdt %0" : "=m" (xdtr) : );
	host_gdt_base = *((unsigned long *) (xdtr + 2));
	host_tr_desc = *((unsigned long *) (host_gdt_base + host_tr_selector));
	val = ((host_tr_desc & 0x000000FFFFFF0000) >> 16) |
	       ((host_tr_desc & 0xFF00000000000000) >> 32);
	host_tr_desc = *((unsigned long *) (host_gdt_base + host_tr_selector + 8));
	host_tr_desc <<= 32;
	val |= host_tr_desc;
	vmcs_set_field(HOST_TR_BASE, val);

	asm volatile ("sgdt %0" : "=m" (xdtr) :);
	val = *((unsigned long *) (xdtr + 2));
	vmcs_set_field(HOST_GDTR_BASE, val);

	asm volatile ("sidt %0" : "=m" (xdtr) :);
	val = *((unsigned long *) (xdtr + 2));
	vmcs_set_field(HOST_IDTR_BASE, val);

	/* HOST_RSP is by vmx_vcpu_load_host_rsp() in __vmx_vcpu_entry() */
	rdmsrq_safe(MSR_IA32_SYSENTER_ESP, (u64 *)&val);
	vmcs_set_field(HOST_IA32_SYSENTER_ESP, val);
	rdmsrq_safe(MSR_IA32_SYSENTER_EIP, (u64 *)&val);
	vmcs_set_field(HOST_IA32_SYSENTER_EIP, val);
	vmcs_set_field(HOST_RIP, (unsigned long)__vmx_vcpu_exit);
}

static void vmx_vcpu_load_ctrl_area(struct vmx_vcpu *vcpu)
{
	struct vmx_vm *vm = vcpu->vm;
	struct vmx_config *config = &vm->config;
	unsigned long val;

	/* EPT */
	vmcs_set_field(VIRTUAL_PROCESSOR_ID, 1);
	vmcs_set_field(EPT_POINTER, vm->ept_pointer);

	/* VM execution, entry and exit controls */
	val = ((0x00000016 | config->pin_based_exec_ctrl[0]) &
	       config->pin_based_exec_ctrl[1]);
	vmcs_set_field(PIN_BASED_VM_EXEC_CONTROL, val);
	val = ((0x840061f2 | config->cpu_based_exec_ctrl[0]) &
	       config->cpu_based_exec_ctrl[1]);
	vmcs_set_field(CPU_BASED_VM_EXEC_CONTROL, val);
	val = ((0x000000a2 | config->cpu_based_2nd_exec_ctrl[0]) &
	       config->cpu_based_2nd_exec_ctrl[1]);
	vmcs_set_field(SECONDARY_VM_EXEC_CONTROL, val);
	val = ((0x000011fb | config->vmentry_ctrl[0]) &
	       config->vmentry_ctrl[1]);
	vmcs_set_field(VM_ENTRY_CONTROLS, val);
	val = ((0x00036ffb | config->vmexit_ctrl[0]) &
	       config->vmexit_ctrl[1]);
	vmcs_set_field(VM_EXIT_CONTROLS, val);
}

/**
 * vmx_vcpu_load - Load vCPU by loading its vmcs block
 *
 * @vcpu: vCPU instance
 */
int vmx_vcpu_load(struct vmx_vcpu *vcpu)
{
	unsigned long vmcs_phys;
	unsigned char retval;

	/* Clear vmcs */
	vmcs_phys = __pa(vcpu->vmcs);
	asm volatile ("vmclear %[vmcs]; setna %[ret]"
			: [ret] "=rm" (retval)
			: [vmcs] "m" (vmcs_phys)
			: "cc", "memory");
	if (retval) {
		pr_warn("%s: Error %02x from vmclear\n", __func__, retval);
		return -EIO;
	}

	/* Load vmcs */
	asm volatile ("vmptrld %[vmcs]; setna %[ret]"
			: [ret] "=rm" (retval)
			: [vmcs] "m" (vmcs_phys)
			: "cc", "memory");
	if (retval) {
		pr_warn("%s: Error %02x from vmptrld\n", __func__, retval);
		return -EIO;
	}

	/* Sync vmcs to vCPU's state */
	vmx_vcpu_load_guest_area(vcpu);
	vmx_vcpu_load_host_area(vcpu);
	vmx_vcpu_load_ctrl_area(vcpu);

	return 0;
}

/**
 * vmx_vcpu_load_host_rsp - Load host's %rsp in vCPU's vmcs block to the
 *                          specified value
 *
 * @vcpu: vCPU instance
 * @host_rsp: host's %rsp
 */
void noinstr vmx_vcpu_load_host_rsp(struct vmx_vcpu *vcpu,
				    unsigned long host_rsp)
{
	vmcs_set_field(HOST_RSP, host_rsp);
}

static void vmx_vcpu_dump_exit_info(struct vmx_vcpu *vcpu)
{
	struct vmx_vcpu_exit_info *info = &vcpu->exit_info;
	struct vmx_vcpu_regs *regs = &vcpu->regs;

	vmx_dbg("=====> %s: guest exits...\n", __func__);
	vmx_dbg("    exit_reason:          %08x\n",   info->exit_reason.full);
        vmx_dbg("    exit_qualification    %016lx\n", info->exit_qualification);
        vmx_dbg("    exit_insn_len         %016lx\n", info->exit_insn_len);
        vmx_dbg("    guest_rip             %016lx\n", info->guest_rip);
        vmx_dbg("    guest_rflags          %016lx\n", info->guest_rflags);
	vmx_dbg("\n");
	vmx_dbg("    rax=%016lx    rbx=%016lx\n", regs->rax, regs->rbx);
	vmx_dbg("    rcx=%016lx    rdx=%016lx\n", regs->rcx, regs->rdx);
	vmx_dbg("    rbp=%016lx    rsi=%016lx\n", regs->rbp, regs->rsi);
	vmx_dbg("    rdi=%016lx\n", regs->rdi);
	vmx_dbg("    r8=%016lx     r9=%016lx\n",  regs->r8,  regs->r9);
	vmx_dbg("    r10=%016lx    r11=%016lx\n", regs->r10, regs->r11);
	vmx_dbg("    r12=%016lx    r13=%016lx\n", regs->r12, regs->r13);
	vmx_dbg("    r14=%016lx    r15=%016lx\n", regs->r14, regs->r15);
	vmx_dbg("\n");
}

static void vmx_vcpu_get_exit_info(struct vmx_vcpu *vcpu)
{
	struct vmx_vcpu_exit_info *info = &vcpu->exit_info;

	info->exit_reason.full   = vmcs_get_field(VM_EXIT_REASON);
	info->exit_qualification = vmcs_get_field(EXIT_QUALIFICATION);
	info->exit_insn_len      = vmcs_get_field(VM_EXIT_INSTRUCTION_LEN);
	info->guest_rip          = vmcs_get_field(GUEST_RIP);
	info->guest_rflags       = vmcs_get_field(GUEST_RFLAGS);
}

static int vmx_vcpu_handle_exit(struct vmx_vcpu *vcpu)
{
	struct vmx_vcpu_exit_info *info = &vcpu->exit_info;
	struct vmx_vcpu_regs *regs = &vcpu->regs;
	int ret = 0;

	switch (info->exit_reason.basic) {
	case 0x0A:	/* cupid */
		regs->rax = 0x6368;
		regs->rbx = 0x6561;
		regs->rcx = 0x70;
		vmcs_set_field(GUEST_RIP, info->guest_rip + info->exit_insn_len);
		break;
	case 0x0C:	/* halt */
		ret = -EIO;
		break;
	default:
		pr_warn("%s: Unhandled exit_reason 0x%x\n",
			__func__, info->exit_reason.basic);
		ret = -ERANGE;
	}

	return ret;
}

/**
 * vmx_vcpu_run - Run the given vCPU instance
 *
 * @vcpu: vCPU instance
 *
 * VM and vCPU should have been loaded before the vCPU becomes runable.
 */
int vmx_vcpu_run(struct vmx_vcpu *vcpu)
{
	int flags = 0;
	int ret = 0;

	do {
		ret = __vmx_vcpu_entry(vcpu, flags);
		if (ret) {
			pr_warn("%s: Error %d to start vCPU\n", __func__, ret);
			break;
		}

		vmx_vcpu_get_exit_info(vcpu);
		vmx_vcpu_dump_exit_info(vcpu);

		ret = vmx_vcpu_handle_exit(vcpu);

		/* vmresume is already used except the first time */
		flags = VMX_VCPU_ENTRY_VMRESUME;
	} while (ret == 0);

	return 0;
}

/**
 * vmx_vcpu_unload - Unload vCPU by clearing its vmcs block on the current
 *                   CPU
 *
 * @vcpu: vCPU instance
 */
void vmx_vcpu_unload(struct vmx_vcpu *vcpu)
{
	unsigned long vmcs_phys;
	unsigned char retval;

	vmcs_phys = __pa(vcpu->vmcs);
	asm volatile ("vmclear %[vmcs]; setna %[ret]"
			: [ret] "=rm" (retval)
			: [vmcs] "m" (vmcs_phys)
			: "cc", "memory");
	if (retval)
                pr_warn("%s: Error 0x%02x from vmclear\n", __func__, retval);
}

/**
 * vmx_vcpu_destroy - Destroy the given vCPU instance
 *
 * @vcpu: vCPU instance
 */
void vmx_vcpu_destroy(struct vmx_vcpu *vcpu)
{
	struct vmx_vm *vm = vcpu->vm;
	int order;

	/* Unlink vCPU from VM */
	vcpu->vm = NULL;
	if (vm)
		vm->vcpu = NULL;

	/* Free vmcs */
	if (vcpu->vmcs) {
		order = get_order(vcpu->vmcs_pages << PAGE_SHIFT);
		free_pages(vcpu->vmcs, order);
	}
}
