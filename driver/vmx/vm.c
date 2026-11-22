// SPDX-License-Identifier: GPL-2.0+
/*
 * VM (Virtual Machine) management
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
#include "guest.h"

static void vmx_dump_config(struct vmx_vm *vm)
{
	struct vmx_config *config = &vm->config;

	vmx_dbg("%s: Dump configurations...\n", __func__);
	vmx_dbg("    MSR_IA32_VMX_BASIC            %08x-%08x\n",
		(unsigned int)(config->basic >> 32), (unsigned int)(config->basic));
	vmx_dbg("    MSR_IA32_VMX_PINBASED_CTLS    %08x:%08x\n",
		config->pin_based_exec_ctrl[1], config->pin_based_exec_ctrl[0]);
	vmx_dbg("    MSR_IA32_VMX_PROCBASED_CTLS   %08x:%08x\n",
		config->cpu_based_exec_ctrl[1], config->cpu_based_exec_ctrl[0]);
	vmx_dbg("    MSR_IA32_VMX_PROCBASED_CTLS2  %08x:%08x\n",
		config->cpu_based_2nd_exec_ctrl[1], config->cpu_based_2nd_exec_ctrl[0]);
	vmx_dbg("    MSR_IA32_VMX_PROCBASED_CTLS3  %08x-%08x\n",
		(unsigned int)(config->cpu_based_3rd_exec_ctrl >> 32),
		(unsigned int)(config->cpu_based_3rd_exec_ctrl));
	vmx_dbg("    MSR_IA32_VMX_ENTRY_CTLS       %08x:%08x\n",
		config->vmentry_ctrl[1], config->vmentry_ctrl[0]);
	vmx_dbg("    MSR_IA32_VMX_EXIT_CTLS        %08x:%08x\n",
		config->vmexit_ctrl[1], config->vmexit_ctrl[0]);
	vmx_dbg("    MSR_IA32_VMX_MISC             %08x-%08x\n",
		(unsigned int)(config->misc >> 32), (unsigned int)(config->misc));
	vmx_dbg("    MSR_IA32_VMX_EPT_VPID_CAP     %08x-%08x\n",
		config->vpid, config->ept);
	vmx_dbg("    MSR_IA32_VMX_CR0_FIXED0       %08x-%08x\n",
		(unsigned int)(config->host_cr0_fixed0 >> 32),
		(unsigned int)(config->host_cr0_fixed0));
	vmx_dbg("    MSR_IA32_VMX_CR0_FIXED1       %08x-%08x\n",
		(unsigned int)(config->host_cr0_fixed1 >> 32),
		(unsigned int)(config->host_cr0_fixed1));
	vmx_dbg("    MSR_IA32_VMX_CR4_FIXED0       %08x-%08x\n",
		(unsigned int)(config->host_cr4_fixed0 >> 32),
		(unsigned int)(config->host_cr4_fixed0));
	vmx_dbg("    MSR_IA32_VMX_CR4_FIXED1       %08x-%08x\n",
		(unsigned int)(config->host_cr4_fixed1 >> 32),
		(unsigned int)(config->host_cr4_fixed1));
}

static void vmx_read_config(struct vmx_vm *vm)
{
	struct vmx_config *config = &vm->config;
	struct msr msr;
	unsigned int val;

	rdmsrq(MSR_IA32_VMX_BASIC, msr.q);
	config->basic = msr.q;

	rdmsrq(MSR_IA32_VMX_PINBASED_CTLS, msr.q);
	config->pin_based_exec_ctrl[0] = msr.l;
	config->pin_based_exec_ctrl[1] = msr.h;
	rdmsrq(MSR_IA32_VMX_PROCBASED_CTLS, msr.q);
	config->cpu_based_exec_ctrl[0] = msr.l;
	config->cpu_based_exec_ctrl[1] = msr.h;

	config->cpu_based_2nd_exec_ctrl[0] = 0;
	config->cpu_based_2nd_exec_ctrl[1] = 0;
	val = CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
	val |= config->cpu_based_exec_ctrl[0];
	val &= config->cpu_based_exec_ctrl[1];
	if (val & CPU_BASED_ACTIVATE_SECONDARY_CONTROLS) {
		rdmsrq(MSR_IA32_VMX_PROCBASED_CTLS2, msr.q);
		config->cpu_based_2nd_exec_ctrl[0] = msr.l;
		config->cpu_based_2nd_exec_ctrl[1] = msr.h;
	}

	config->cpu_based_3rd_exec_ctrl = 0UL;
	val = CPU_BASED_ACTIVATE_TERTIARY_CONTROLS;
	val |= config->cpu_based_exec_ctrl[0];
	val &= config->cpu_based_exec_ctrl[1];
	if (val & CPU_BASED_ACTIVATE_TERTIARY_CONTROLS) {
		rdmsrq(MSR_IA32_VMX_PROCBASED_CTLS3, msr.q);
		config->cpu_based_3rd_exec_ctrl = msr.q;
	}

	rdmsrq(MSR_IA32_VMX_ENTRY_CTLS, msr.q);
	config->vmentry_ctrl[0] = msr.l;
	config->vmentry_ctrl[1] = msr.h;
	rdmsrq(MSR_IA32_VMX_EXIT_CTLS, msr.q);
	config->vmexit_ctrl[0] = msr.l;
	config->vmexit_ctrl[1] = msr.h;

	rdmsrq(MSR_IA32_VMX_MISC, msr.q);
	config->misc = msr.q;

	rdmsrq(MSR_IA32_VMX_EPT_VPID_CAP, msr.q);
	config->vpid = msr.h;
	config->ept = msr.l;

	/* Host state fixups */
	rdmsrq(MSR_IA32_VMX_CR0_FIXED0, msr.q);
	config->host_cr0_fixed0 = msr.q;
	rdmsrq(MSR_IA32_VMX_CR0_FIXED1, msr.q);
	config->host_cr0_fixed1 = msr.q;
	rdmsrq(MSR_IA32_VMX_CR4_FIXED0, msr.q);
	config->host_cr4_fixed0 = msr.q;
	rdmsrq(MSR_IA32_VMX_CR4_FIXED1, msr.q);
	config->host_cr4_fixed1 = msr.q;
}

static void vmx_vm_free_mem(struct vmx_vm *vm)
{
	struct vmx_vm_memory *mem = &vm->memory;
	int order;

	if (mem->ept_memory) {
		order = get_order(mem->ept_memory_pages << PAGE_SHIFT);
		free_pages(mem->ept_memory, order);
		mem->ept_memory = 0UL;
	}

	if (mem->guest_memory) {
		order = get_order(mem->guest_memory_pages << PAGE_SHIFT);
		free_pages(mem->guest_memory, order);
		mem->guest_memory = 0UL;
	}

	if (mem->root_vmcs) {
		order = get_order(mem->root_vmcs_pages << PAGE_SHIFT);
		free_pages(mem->root_vmcs, order);
		mem->root_vmcs = 0UL;
	}
}

static int vmx_vm_alloc_mem(struct vmx_vm *vm)
{
	struct vmx_config *config = &vm->config;
	struct vmx_vm_memory *mem = &vm->memory;
	struct vmcs *vmcs;
	struct page *page;
	int i, order;

	/* All the sizes of required memory blocks are fixed */
	mem->root_vmcs_pages = 1;
	mem->guest_memory_pages = 16;
	mem->ept_memory_pages = 4;

	/* Root vmcs */
	order = get_order(mem->root_vmcs_pages << PAGE_SHIFT);
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!page) {
		pr_warn("%s: Unable to alloc root vmcs\n", __func__);
		goto error;
	}

	mem->root_vmcs = (unsigned long)page_address(page);
	vmcs = (struct vmcs *)(mem->root_vmcs);
	vmcs->hdr.revision_id = vmx_basic_vmcs_revision_id(config->basic);

	/* Guest memory */
	order = get_order(mem->guest_memory_pages << PAGE_SHIFT);
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!page) {
		pr_warn("%s: Unable to alloc guest memory\n", __func__);
		goto error;
	}

	mem->guest_memory = (unsigned long)page_address(page);
	for (i = 0; i < ARRAY_SIZE(guest_bin); i++)
		*((unsigned char *)(mem->guest_memory + i)) = guest_bin[i];

	/* EPT memory */
	order = get_order(mem->ept_memory_pages << PAGE_SHIFT);
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!page) {
		pr_warn("%s: Unable to alloc EPT memory\n", __func__);
		goto error;
	}

	mem->ept_memory = (unsigned long)page_address(page);

	return 0;

error:
	vmx_vm_free_mem(vm);
	return -ENOMEM;
}

static void vmx_vm_init_ept(struct vmx_vm *vm)
{
	struct vmx_vm_memory *mem = &vm->memory;
	unsigned long *entry, i;
	unsigned long pa = __pa(mem->ept_memory);

	/* PML4 entry */
	entry = (unsigned long *)(mem->ept_memory);
	*entry = (pa + PAGE_SIZE) | BIT(2) | BIT(1) | BIT(0);

	/* PDP entry */
	entry = (unsigned long *)(mem->ept_memory + PAGE_SIZE);
	*entry = (pa + 2 * PAGE_SIZE) | BIT(2) | BIT(1) | BIT(0);

	/* PDE */
	entry = (unsigned long *)(mem->ept_memory + 2 * PAGE_SIZE);
	*entry = (pa + 3 * PAGE_SIZE) | BIT(2) | BIT(1) | BIT(0);

	/* PTEs */
	pa = __pa(mem->guest_memory);
	for (i = 0; i < mem->guest_memory_pages; i++) {
		entry = (unsigned long *)(mem->ept_memory + 3 * PAGE_SIZE + i * 8);
		*entry = pa | (6 << 3) | BIT(2) | BIT(1) | BIT(0);

		pa += PAGE_SIZE;
	}

	pa = __pa(mem->ept_memory);
	vm->ept_pointer = (pa | BIT(6) | (3 << 3) | 6);
}

/**
 * vmx_vm_init - Initialize a VM
 *
 * @vm: VM instance
 */
int vmx_vm_init(struct vmx_vm *vm)
{
	int ret;

	/* Assume the configurations are same among all CPUs */
	vmx_read_config(vm);
	vmx_dump_config(vm);

	/*
	 * Alloc the required memory. At present, everything is
	 * static and fixed and it's to be improved in the future.
	 */
	ret = vmx_vm_alloc_mem(vm);
	if (ret)
		return ret;

	/* Initialize EPT based the memory layout */
	vmx_vm_init_ept(vm);

	return 0;
}

/**
 * vmx_vm_load - Load VM by loading the root vmcs on the current CPU
 *
 * @vm: VM instance
 */
int vmx_vm_load(struct vmx_vm *vm)
{
	struct vmx_vm_memory *mem = &vm->memory;
	unsigned long val, vmcs_phys;
	unsigned char retval;

	/* Enable VMX on CR4 */
	asm volatile ("mov %%cr4, %0" : "=r" (val));
	val |= X86_CR4_VMXE;
	asm volatile ("mov %0, %%cr4": "+r" (val) :: "memory");

	/* Load the root vmcs */
	vmcs_phys = __pa(mem->root_vmcs);
	asm volatile ("vmxon %[vmcs]; setna %[ret]"
			: [ret] "=rm" (retval)
			: [vmcs] "m" (vmcs_phys)
			: "cc", "memory");
	if (retval) {
		asm volatile ("mov %%cr4,%0" : "=r" (val));
		val &= ~X86_CR4_VMXE;
		asm volatile ("mov %0, %%cr4": "+r" (val) :: "memory");
		pr_warn("%s: Error %02x from vmxon\n", __func__, retval);
		return -EIO;
	}

	return 0;
}

/**
 * vmx_vm_unload - Unload VM by disabling VMX on the current CPU
 *
 * @vm: VM instance
 */
void vmx_vm_unload(struct vmx_vm *vm)
{
	unsigned long val;

	asm volatile ("vmxoff" ::: "cc", "memory");

	/* Disable vmx on CR4 */
	asm volatile ("mov %%cr4, %0" : "=r" (val));
	val &= ~X86_CR4_VMXE;
	asm volatile ("mov %0, %%cr4": "+r" (val) : : "memory");
}

/**
 * vmx_vm_destroy - Destroy a VM
 *
 * @vm: VM instance
 */
void vmx_vm_destroy(struct vmx_vm *vm)
{
	vmx_vm_free_mem(vm);
}
