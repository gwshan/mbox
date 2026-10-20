#
#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Launch a VM as specified by JSON configuration file
#
# Copyright (c) 2026 Red Hat, Inc.
#
# Author: Gavin Shan <gshan@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
#

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from lib.mm.hugetlbfs import HugeTLBSetting

def get_mem_size(size="", prefix="", suffix=""):
    multipliers = {
        'k': 1024,    'kb': 1024,
        'm': 1024**2, 'mb': 1024**2,
        'g': 1024**3, 'gb': 1024**3,
        't': 1024**4, 'tb': 1024**4
    }

    # Remove the prefix and suffix
    if prefix != "":
        size = size.removeprefix(prefix)
    if suffix != "":
        size = size.removesuffix(suffix)

    # Directly conversoin if there is no trailing unit
    if size.isdigit():
        return (int)(size)

    # Indirectly conversion with the unit considered
    match = re.match(r"^([0-9]+)\s*([a-z]+)$", size.strip().lower())
    if not match:
        print(f"WARN: Invalid size string <{size}>")
        return 0

    n = int(match.group(1))
    unit = match.group(2)
    if unit not in multipliers:
        print(f"WARN: Invalid size string <{size}>")
        return 0

    return (int)(n * multipliers[unit])

def reserve_hugetlb_mem(size, hpage_size):
    # Accumulative number of huge pages
    if not hasattr(reserve_hugetlb_mem, "hpages"):
        reserve_hugetlb_mem.hpages = 0

    # Convert huge page size to KB
    hpage_size = (int)(hpage_size / 1024)

    # Validate the huge page size
    if not Path(f"/sys/kernel/mm/hugepages/hugepages-{hpage_size}kB").exists():
        print(f"WARN: Invalid huge page size {hpage_size}")
        return False

    # Check if the filesystem has been mounted
    is_mounted = False
    path = "/dev/hugepages-" + str(hpage_size) + "kB"
    with open("/proc/mounts", "r") as f:
        for line in f:
            fields = line.strip().split()
            if fields[1] == path and fields[2] == "hugetlbfs":
                is_mounted = True
                break;

    # Mount the filesystem if needed
    if not is_mounted:
        result = subprocess.run(f"mkdir -p {path}", shell=True,
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"WARN: Unable to create directory <{path}>")
            return False

        result = subprocess.run(f"mount -thugetlbfs none -o 'pagesize={hpage_size}kB' {path}",
                                shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"WARN: Unable to mount {path}")
            return False

    # Accumulative number of the huge pages
    hpages = int((size + hpage_size * 1024 - 1) / (hpage_size * 1024))
    reserve_hugetlb_mem.hpages += hpages;
    hpages = reserve_hugetlb_mem.hpages

    # Check if we already adaquate huge pages
    with open(f"/sys/kernel/mm/hugepages/hugepages-{hpage_size}kB/free_hugepages", 'r') as f:
        nr_free_hpages = int(f.read().strip())

    if nr_free_hpages >= hpages:
        return True

    # Reserve huge pages with the free huge pages resued
    hpages -= nr_free_hpages
    with open(f"/sys/kernel/mm/hugepages/hugepages-{hpage_size}kB/nr_hugepages", 'r') as f:
         nr_hpages = int(f.read().strip())
    with open(f"/sys/kernel/mm/hugepages/hugepages-{hpage_size}kB/nr_hugepages", 'w') as f:
         f.write(str(nr_hpages + hpages))

    return True

def get_network_mac_addr(bridge="br0"):
    path = Path(f"/sys/class/net/{bridge}")
    path1 = Path(f"/sys/class/net/{bridge}/bridge")
    if not (path.exists() and path1.exists()):
        print(f"WARN: Bridge <{bridge}> not exist")
        return ""

    # Figure out the MAC address
    with open(f"/sys/class/net/{bridge}/address", "r") as f:
        mac_addr = f.read()
    mac_addr_fields = mac_addr.split(":")
    mac_addr_fields[-1] = "80"
    mac_addr = ":".join(mac_addr_fields)

    # Create /etc/qemu-ifup if needed
    path = Path(f"/etc/qemu-ifup")
    if not path.exists():
        with open(f"/etc/qemu-ifup", "w") as f:
            f.write(f"#!/bin/sh")
            f.write(f"")
            f.write(f"ip link set dev $1 addr {mac_addr}")
            f.write(f"ip link set $1 up")
            f.write(f"ip link set dev $1 master {bridge}")
            f.write(f"ip link set {bridge} type bridge forward_delay 0")
            f.write(f"ip link set {bridge} type bridge stp_state 0" )

            path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    # Create /etc/qemu-ifdown if needed
    path = Path(f"/etc/qemu-ifdown")
    if not path.exists():
        with open(f"/etc/qemu-ifdown", "w") as f:
            f.write(f"#!/bin/sh")
            f.write(f"")
            f.write(f"ip link set $1 down")
            f.write(f"ip link set dev $1 nomaster")

            path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    return mac_addr

def build_opt(obj, key, prefix, suffix=""):
    if obj is None or key is None or key == "":
        return ""

    return f"{prefix}{obj.get(key)}{suffix}"

def build_command(vm):
    pcie_idx = 1
    pcie_total = 0
    cmd = ""

    # Executable binary
    cmd = vm.get("binary") + " "

    # Accelerator
    for i, obj in enumerate(vm.get("accel", [])):
        cmd += build_opt(obj, "type", "-accel ")
        cmd += " "

    # Machine
    for i, obj in enumerate(vm.get("machine", [])):
        cmd += build_opt(obj, "type", "-machine ")
        cmd += build_opt(obj, "gic-version", ",gic-version=")
        cmd += build_opt(obj, "nvdimm", ",nvdimm=")
        cmd += " "

    # CPU and SMP
    for i, obj in enumerate(vm.get("cpu", [])):
        cmd += build_opt(obj, "type", "-cpu ")
        cmd += " "
        for j, child in enumerate(obj.get("smp", [])):
            cmd += build_opt(child, "maxcpus", "-smp maxcpus=")
            cmd += build_opt(child, "cpus", ",cpus=")
            cmd += build_opt(child, "sockets", ",sockets=")
            cmd += build_opt(child, "clusters", ",clusters=")
            cmd += build_opt(child, "cores", ",cores=")
            cmd += build_opt(child, "threads", ",threads=")
            cmd += " "

    # Memory: memory-backend and NUMA nodes
    for i, obj in enumerate(vm.get("memory", [])):
        cmd += build_opt(obj, "mem", "-m ")
        cmd += build_opt(obj, "maxmem", ",maxmem=")
        cmd += build_opt(obj, "slots", ",slots=", " ")

        # mem-slots
        for j, child in enumerate(obj.get("mem-slots", [])):
            mem_type = child.get("type")
            mem_path = child.get("mem-path")
            if child.get("available") != "yes":
                continue

            if  mem_type == "ram":
                # memory-backend-ram
                cmd += build_opt(child, "id", "-object memory-backend-ram,id=")
                cmd += build_opt(child, "size", ",size=", " ")
            elif mem_type == "file" and mem_path.startswith("hugetlbfs-"):
                # hugetlbfs
                hpage_size = get_mem_size(mem_path, "hugetlbfs-", "")
                size = get_mem_size(child.get("size"), "", "")
                if hpage_size == 0 or size == 0:
                    continue

                ret = reserve_hugetlb_mem(size, hpage_size)
                if not ret:
                    continue

                cmd += build_opt(child, "id", "-object memory-backend-file,id=")
                cmd += build_opt(child, "size", ",size=")
                cmd += ",mem-path=" + "/dev/hugepages-" + str((int)(hpage_size / 1024)) + "kB"
                cmd += ",prealloc=on "
            elif mem_type == "file":
                # regular file
                if mem_path == "" or not Path(mem_path).exists():
                    print(f"WARN: <mem-path> for mem-slot <{child.get('id')}> not exist")
                    continue

                cmd += build_opt(child, "id", "-object memory-backend-file,id=")
                cmd += build_opt(child, "size", ",size=")
                cmd += "mem-path=" + mem_path + ",share=on "
            else:
                print(f"WARN: Unsupported mem-slot backend <{mem_type}>")        
       
        # numa-nodes
        for j, child in enumerate(obj.get("numa-nodes", [])):
            if child.get("available") != "yes":
                continue

            cmd += build_opt(child, "nodeid", "-numa node,nodeid=")
            cmd += build_opt(child, "memdev", ",memdev=")
            cmd += build_opt(child, "cpus", ",cpus=")
            cmd += " "

    # Image, serial/gdb/qmp port
    for i, obj in enumerate(vm.get("image", [])):
        cmd += build_opt(obj, "firmware_path", "-L ", " ")
        cmd += build_opt(obj, "firmware", "-bios ", " ")
        cmd += build_opt(obj, "kernel", "-kernel ", " ")
        cmd += build_opt(obj, "initrd", "-initrd ", " ")

    cmd += "-monitor none -serial mon:stdio -nographic "
    cmd += build_opt(vm, "gdb_tcp_port", "-gdb tcp::", " ")
    cmd += build_opt(vm, "qmp_tcp_port", "-qmp tcp:localhost:", ",server,wait=off ")

    # PCIe root ports
    pci_dev_list = []
    pci_dev_list.append("virtio-blk-pci")
    pci_dev_list.append("virtio-net-pci")
    pci_dev_list.append("virtio-mem-pci")
    pci_dev_list.append("virtio-pmem-pci")
    pci_dev_list.append("virtio-balloon-pci")
    pci_dev_list.append("virtio-rng-pci")
    pci_dev_list.append("pvpanic-pci")
    pci_dev_list.append("pci-testdev")

    pcie_total = 1
    for i, obj in enumerate(vm.get("device", [])):
        if obj.get("available") != "yes" \
           or not obj.get("type") in pci_dev_list:
            continue

        pcie_total += 1

    if pcie_total > 1:
        for pcie_idx in range(1, pcie_total):
            cmd += "-device pcie-root-port,id=pcie." + str(pcie_idx) + \
                   ",bus=pcie.0,chassis=" + str(pcie_idx) + " "

    # Devices
    pcie_idx = 1
    for i, obj in enumerate(vm.get("device", [])):
        if obj.get("available") != "yes":
            continue

        if obj.get("type") == "nvdimm":
            # nvdimm
            if not Path(obj.get("mem-path")).exists():
                print(f"WARN: mem-path <{obj.get('mem-path')}> not exist nvdimm")
                continue

            cmd += build_opt(obj, "id", "-object memory-backend-file,id=", "-mem")
            cmd += build_opt(obj, "mem-path", ",mem-path=")
            cmd += build_opt(obj, "size", ",size=", ",share=on ")
            cmd += build_opt(obj, "id", "-device nvdimm,id=")
            cmd += build_opt(obj, "id", ",memdev=", "-mem")
            cmd += build_opt(obj, "slot", ",slot=")
            cmd += build_opt(obj, "node", ",node=")
            cmd += " "
        elif obj.get("type") == "virtio-blk-pci":
            # virtio-blk-pci devices
            if not Path(obj.get("file")).exists():
                print(f"WARN: File <{obj.get("file")}> not exist for virtio-blk-pci")
                continue

            cmd += build_opt(obj, "file", "-drive file=")
            cmd += build_opt(obj, "id", ",id=", "-drive,if=none ")
            cmd += build_opt(obj, "id", "-device virtio-blk-pci,id=")
            cmd += ",bus=pcie." + str(pcie_idx)
            cmd += build_opt(obj, "id", ",drive=", "-drive,num-queues=4 ")
            cmd += "-boot c"

            pcie_idx += 1
        elif obj.get("type") == "virtio-net-pci":
            # virtio-net-pci devices
            mac_addr = get_network_mac_addr("br0")
            if mac_addr == "":
                continue

            cmd += build_opt(obj, "id", "-netdev tap,id=", "-tap")
            cmd += build_opt(obj, "vhost", ",vhost=")
            cmd += ",script=/etc/qemu-ifup" + ",downscript=/etc/qemu-ifdown "
            cmd += build_opt(obj, "id", "-device virtio-net-pci,id=")
            cmd += ",bus=pcie." + str(pcie_idx)
            cmd += build_opt(obj, "id", ",netdev=", "-tap")
            cmd += ",mac=" + mac_addr + " "

            pcie_idx += 1
        elif obj.get("type") == "virtio-mem-pci":
            # virtio-mem-pci
            cmd += build_opt(obj, "id", "-object memory-backend-ram,id=", "-mem")
            cmd += build_opt(obj, "size", ",size=", " ")
            cmd += build_opt(obj, "id", "-device virtio-mem-pci,id=")
            cmd += ",bus=pcie." + str(pcie_idx)
            cmd += build_opt(obj, "id", ",memdev=", "-mem")
            cmd += build_opt(obj, "requested-size", ",requested-size=")
            cmd += build_opt(obj, "node", ",node=", " ")

            pcie_idx += 1
        elif obj.get("type") == "virtio-pmem-pci":
            # virtio-pmem-pci
            cmd += build_opt(obj, "id", "-object memory-backend-ram,id=", "-mem")
            cmd += build_opt(obj, "size", ",size=", " ")
            cmd += build_opt(obj, "id", "-device virtio-pmem-pci,id=")
            cmd += ",bus=pcie." + str(pcie_idx)
            cmd += build_opt(obj, "id", ",memdev=", "-mem")
            cmd += build_opt(obj, "node", ",node=", " ")

            pcie_idx += 1
        elif obj.get("type") == "virtio-balloon-pci":
            # virtio-balloon-pci
            cmd += build_opt(obj, "id", "-device virtio-balloon-pci,id=")
            cmd += ",bus=pcie." + str(pcie_idx)
            cmd += build_opt(obj, "free-page-reporting", ",free-page-reporting=", " ")

            pcie_idx += 1
        elif obj.get("type") == "virtio-rng-pci":
            # virtio-rng-pci
            cmd += build_opt(obj, "id", "-object rng-random,id=", "-rng")
            cmd += build_opt(obj, "filename", ",filename=", " ")
            cmd += build_opt(obj, "id", "-device virtio-rng-pci,id=")
            cmd += ",bus=pcie." + str(pcie_idx)
            cmd += build_opt(obj, "id", ",rng=", "-rng ")

            pcie_idx += 1
        elif obj.get("type") == "pvpanic-pci":
            # pvpanic-pci
            cmd += build_opt(obj, "id", "-device pvpanic-pci,id=")
            cmd += ",bus=pcie." + str(pcie_idx) + " "

            pcie_idx += 1
        elif obj.get("type") == "pci-testdev":
            # test-pci
            cmd += build_opt(obj, "id", "-device pci-testdev,id=")
            cmd += ",bus=pcie." + str(pcie_idx) + " "

            pcie_idx += 1
        elif obj.get("type") == "nvme":
            # nvme
            if not Path(obj.get("file")).exists():
                continue

            cmd += build_opt(obj, "file", "-drive file=")
            cmd += build_opt(obj, "id", ",id=", "-drive,if=none,format=qcow2 ")
            cmd += build_opt(obj, "id", "-device nvme,serial=")
            cmd += build_opt(obj, "id", ",drive=", "-drive ")

    return cmd

def main():
    parser = argparse.ArgumentParser(description="Run VM")
    parser.add_argument("--vm", help="VM template to be loaded")
    parser.add_argument("--dry-run", action="store_true", help="Only check the command lines")

    # Validate the input arguments
    args = parser.parse_args()
    if args.vm is None:
        print(f"ERROR: VM template is needed by '--vm' option")
        sys.exit(1)

    # Check if the VM template file exists
    file_path = Path("vm.json")
    if not file_path.exists():
        print(f"error: vm.json doesn't exist")
        sys.exit(1)

    # Load the section from the template for the specified VM
    with open(file_path) as f:
        data = json.load(f)

    vms = data.get("vms", [])
    if not vms:
        print(f"error: no <vms> in vm.json")
        sys.exit(1)

    vms = [v for v in vms if v["name"] == args.vm]
    if not vms:
        print(f"error: vm '{args.vm}' not found")
        sys.exit(1)

    # Launch the VM
    for vm in vms:
        # Save hugetlb setting
        hugetlb_setting = HugeTLBSetting()
        hugetlb_setting.save()

        # Build the command lines
        cmd = build_command(vm)
        if args.dry_run:
            print(f"")
            print(f"The generated command lines:")
            print(f"")
            print(f"{cmd}")
        else:
             subprocess.run(cmd, shell=True, check=True)

        # Restore hugetlb setting
        hugetlb_setting.restore()

if __name__ == "__main__":
    main()
