// SPDX-License-Identifier: GPL-2.0+
/*
 * Header for structures to maintain VM and vCPU based on Intel's
 * vmx (VMCS block).
 *
 * Author: Gavin Shan <shan.gavin@gmail.com> 2026
 */
#ifndef __VMX_H__
#define __VMX_H__

/**
 * struct vmx_config - Intel VMX global configurations retrieved from
 *                     MSRs with the assumption of symmetric settings
 *                     acorss multiple CPUs.
 * @basic: MSR_IA32_VMX_BASIC
 * @pin_based_exec_ctrl: MSR_IA32_VMX_PINBASED_CTLS
 * @cpu_based_exec_ctrl: MSR_IA32_VMX_PROCBASED_CTLS
 * @cpu_based_2nd_exec_ctrl: MSR_IA32_VMX_PROCBASED_CTLS2
 * @cpu_based_3rd_exec_ctrl: MSR_IA32_VMX_PROCBASED_CTLS3
 * @vmentry_ctrl: MSR_IA32_VMX_ENTRY_CTLS
 * @vmexit_ctrl: MSR_IA32_VMX_EXIT_CTLS
 * @misc: MSR_IA32_VMX_MISC
 * @ept: Lower 32-bits of MSR_IA32_VMX_EPT_VPID_CAP
 * @vpid: Upper 32-bits of MSR_IA32_VMX_EPT_VPID_CAP
 * @host_cr0_fixed0: MSR_IA32_VMX_CR0_FIXED0
 * @host_cr0_fixed1: MSR_IA32_VMX_CR0_FIXED1
 * @host_cr4_fixed0: MSR_IA32_VMX_CR4_FIXED0
 * @host_cr4_fixed1: MSR_IA32_VMX_CR4_FIXED1
 */
struct vmx_config {
	unsigned long	basic;
	unsigned int	pin_based_exec_ctrl[2];
	unsigned int	cpu_based_exec_ctrl[2];
	unsigned int	cpu_based_2nd_exec_ctrl[2];
	unsigned long	cpu_based_3rd_exec_ctrl;
	unsigned int	vmentry_ctrl[2];
	unsigned int	vmexit_ctrl[2];
	unsigned long	misc;

	unsigned int	ept;
	unsigned int	vpid;

	unsigned long	host_cr0_fixed0;
	unsigned long	host_cr0_fixed1;
	unsigned long	host_cr4_fixed0;
	unsigned long	host_cr4_fixed1;
};

/**
 * struct vmx_vm_memory - Track the allocated memory for virtual machine (VM)
 *
 * @root_vmcs_pages: Number of pages for root vmcs
 * @root_vmcs: Root vmcs for 'vmxon' instruction
 * @guest_memory_pages: Number of pages for guest
 * @guest_memory: Memory for the guest
 * @ept_memory_pages: Number of pages for EPT
 * @ept_memory: Memory for EPT
 */
struct vmx_vm_memory {
	unsigned long   root_vmcs_pages;
	unsigned long   root_vmcs;
	unsigned long   guest_memory_pages;
	unsigned long   guest_memory;
	unsigned long   ept_memory_pages;
	unsigned long   ept_memory;
};

/**
 * struct vmx_vm - VMX based virtual machine (VM)
 *
 * @config: VMX configurations figured out from MSRs
 * @memory: Allocated memory used by the VM
 * @vcpu: vCPU instance, only one vCPU is supported
 * @ept_pointer: Pointer of EPT loaded to vmcs block
 */
struct vmx_vcpu;
struct vmx_vm {
	struct vmx_config	config;
	struct vmx_vm_memory	memory;
	struct vmx_vcpu		*vcpu;
	unsigned long		ept_pointer;
};

/**
 * struct vmx_vcpu_regs - VMX vCPU registers which are saved and loaded on
 *                        vCPU exit and entry.
 */
struct vmx_vcpu_regs {
	unsigned long   rax;
	unsigned long   rbx;
	unsigned long   rcx;
	unsigned long   rdx;
	unsigned long   rbp;
	unsigned long   rsp;
	unsigned long   rsi;
	unsigned long   rdi;
	unsigned long   r8;
	unsigned long   r9;
	unsigned long   r10;
	unsigned long   r11;
	unsigned long   r12;
	unsigned long   r13;
	unsigned long   r14;
	unsigned long   r15;
};

/**
 * union vmx_vcpu_exit_reason - vCPU exit reason pulled from VM_EXIT_REASON
 *                              field of vmcs block
 */
union vmx_vcpu_exit_reason {
	struct {
		unsigned int basic              : 16;
		unsigned int reserved           : 10;
		unsigned int bus_lock_detected  : 1;
		unsigned int enclave_mode       : 1;
		unsigned int smi_pending_mtf    : 1;
		unsigned int smi_from_vmx_root  : 1;
		unsigned int reserved30         : 1;
		unsigned int failed_vmentry     : 1;
	};
	unsigned int full;
};

/**
 * struct vmx_vcpu_exit_info - vCPU exit information read from vmcs block
 */
struct vmx_vcpu_exit_info {
	union vmx_vcpu_exit_reason	exit_reason;
	unsigned long			exit_qualification;
	unsigned long			exit_insn_len;
	unsigned long			guest_rip;
	unsigned long			guest_rflags;
};

/**
 * struct vmx_vcpu - VMX vCPU
 *
 * @vm: Parent VM (Virtual Machine) of this vCPU
 * @vmcs_pages: Number of pages for vmcs block
 * @vmcs: vmcs block
 * @regs: vCPU registers
 * @exit_info: vCPU exit info read from vmcs block
 */
struct vmx_vcpu {
	struct vmx_vm			*vm;
	unsigned long			vmcs_pages;
	unsigned long			vmcs;
	struct vmx_vcpu_regs		regs;
	struct vmx_vcpu_exit_info	exit_info;
};

/* Flags passed to __vmx_vcpu_entry() */
#define VMX_VCPU_ENTRY_VMRESUME		BIT(0)

extern bool vmx_debug;
#define vmx_dbg(x...)			\
	do {				\
		if (vmx_debug)		\
			pr_info(x);	\
	} while (0)

int vmx_vm_init(struct vmx_vm *vm);
int vmx_vm_load(struct vmx_vm *vm);
void vmx_vm_unload(struct vmx_vm *vm);
void vmx_vm_destroy(struct vmx_vm *vm);

int vmx_vcpu_init(struct vmx_vm *vm, struct vmx_vcpu *vcpu);
int vmx_vcpu_load(struct vmx_vcpu *vcpu);
void vmx_vcpu_load_host_rsp(struct vmx_vcpu *vcpu, unsigned long host_rsp);
bool __vmx_vcpu_entry(struct vmx_vcpu *vcpu, unsigned int flags);
void __vmx_vcpu_exit(void);
int vmx_vcpu_run(struct vmx_vcpu *vcpu);
void vmx_vcpu_unload(struct vmx_vcpu *vcpu);
void vmx_vcpu_destroy(struct vmx_vcpu *vcpu);

#endif /* __VMX_H__ */
