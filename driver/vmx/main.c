// SPDX-License-Identifier: GPL-2.0+
/*
 * Simplified driver to run a hunk of guest code on top of Intel's VMX.
 * It's only for experimental purpose to illustrate how the vmcs control
 * block is initialized and launched. It may be extendeded in future to
 * support device emulation when I have more free time.
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

#define DRIVER_VERSION  "0.1"
#define DRIVER_AUTHOR   "Gavin Shan, Redhat Inc"
#define DRIVER_DESC     "Driver to intercept user process with an execution of guest code"

bool vmx_debug = true;
static struct vmx_vm vm;
static struct vmx_vcpu vcpu;

static long vmx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;

	ret = vmx_vm_load(&vm);
	if (ret)
		return -EIO;

	ret = vmx_vcpu_load(&vcpu);
	if (ret)
		return -EIO;

	vmx_vcpu_run(&vcpu);

	vmx_vcpu_unload(&vcpu);
	vmx_vm_unload(&vm);

	return 0;
}

static const struct file_operations vmx_fops = {
	.unlocked_ioctl = vmx_ioctl,
};

static struct miscdevice vmx_dev = {
	.name	= "test",
	.minor	= MISC_DYNAMIC_MINOR,
	.fops	= &vmx_fops,
};

static int __init vmx_init(void)
{
	int ret;

	vmx_dbg("\n");
	vmx_dbg("==================== vmx_init ====================\n");

	ret = vmx_vm_init(&vm);
	if (ret)
		return ret;

	ret = vmx_vcpu_init(&vm, &vcpu);
	if (ret)
		goto destroy_vmx_vm;

	/* Register a misc device */
	ret = misc_register(&vmx_dev);
	if (ret) {
		pr_warn("%s: Error %d to register misc dev\n", __func__, ret);
		goto destroy_vmx_vcpu;
	}

	return 0;

destroy_vmx_vm:
	vmx_vm_destroy(&vm);
destroy_vmx_vcpu:
	vmx_vcpu_destroy(&vcpu);

	return ret;
}

static void __exit vmx_exit(void)
{
	vmx_vcpu_destroy(&vcpu);
	vmx_vm_destroy(&vm);
}

module_init(vmx_init);
module_exit(vmx_exit);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
