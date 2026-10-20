#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# HugeTLBSetting to save and restore HugeTLBFs settings
#
# Copyright (c) 2026 Red Hat, Inc.
#
# Authors:
#  Gavin Shan <gshan@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
#

from dataclasses import dataclass, field
from pathlib import Path

HUGETLB_SYSFS_PATH = "/sys/kernel/mm/hugepages"
HUGETLB_NODE_SYSFS_PATH = "/sys/devices/system/node"

@dataclass
class HugeTLBConfig:
    hugepage_size: int
    demote_size: int
    nr_hugepages: int
    nr_hugepages_mempolicy: int
    nr_overcommit_hugepages: int

@dataclass
class HugeTLBNodeConfig:
    node_id: int
    hugepage_size: int
    nr_hugepages: int
    demote_size: int

@dataclass
class HugeTLBSetting:
    configs: list[HugeTLBConfig] = field(default_factory=list)
    node_configs: list[HugeTLBNodeConfig] = field(default_factory=list)
    saved: bool = False

    def save(self):
        # Bail if the configurations have been saved
        if self.saved:
            return

        # The base configurations from "/sys/kernel/mm/hugepages"
        root = Path(HUGETLB_SYSFS_PATH)
        if not root.is_dir():
            raise FileNotFoundError(f"hugetlbfs sysfs not found: {root}")

        for entry in sorted(root.iterdir()):
            if not entry.name.startswith("hugepages-"):
                continue

            hugepage_size = int(entry.name.removeprefix("hugepages-").removesuffix("kB"))
            nr_huepages = int(0)
            nr_hugepages_mempolicy = int(0)
            nr_overcommit_hugepages = int(0)
            demote_size = int(0)

            if (entry / "nr_hugepages").exists():
                nr_hugepages = int((entry / "nr_hugepages").read_text().strip())
            if (entry / "nr_hugepages_mempolicy").exists():
                nr_hugepages_mempolicy = int((entry / "nr_hugepages_mempolicy").read_text().strip())
            if (entry / "nr_overcommit_hugepages").exists():
                nr_overcommit_hugepages = int((entry / "nr_overcommit_hugepages").read_text().strip())
            if (entry / "demote_size").exists():
                demote_size = int((entry / "demote_size").read_text().strip().removesuffix("kB"))

            self.configs.append(HugeTLBConfig(hugepage_size, demote_size,
                                              nr_hugepages, nr_hugepages_mempolicy,
                                              nr_overcommit_hugepages))

        # Node specific configurations from "/sys/devices/system/node"
        root = Path(HUGETLB_NODE_SYSFS_PATH)
        if not root.is_dir():
             raise FileNotFoundError(f"hugetlbfs sysfs not found: {root}")

        for node in sorted(root.iterdir()):
            if not node.name.startswith("node"):
                continue

            node_id = int(node.name.removeprefix("node").strip())
            hugepages = (node / "hugepages")
            for entry in sorted(hugepages.iterdir()):
                if not entry.name.startswith("hugepages-"):
                    continue

                hugepage_size = (int)(entry.name.removeprefix("hugepages-").removesuffix("kB").strip())
                nr_hugepages = int(0)
                demote_size = int(0)
                if (entry / "nr_hugepages").exists():
                    nr_hugepages = (int)((entry / "nr_hugepages").read_text().strip())
                if (entry / "demote_size").exists():
                    demote_size = (int)((entry / "demote_size").read_text().strip().removesuffix("kB"))

                self.node_configs.append(HugeTLBNodeConfig(node_id, hugepage_size,
                                                           nr_hugepages, demote_size))

        # Indicator that the configurations have been saved
        self.saved = True

    def restore(self):
        if not self.saved:
            return

        # The base configurations to "/sys/kernel/mm/hugepages"
        root = Path(HUGETLB_SYSFS_PATH)
        while len(self.configs) > 0:
            config = self.configs.pop()
            entry = (root / f"hugepages-{config.hugepage_size}kB")
            # FIXME: val >= PMD_SIZE and val < config.hugepage_size
            # if (entry / "demote_size").exists():
            #    (entry / "demote_size").write_text(f"{config.demote_size}kB")
            if (entry / "nr_hugepages").exists():
                (entry / "nr_hugepages").write_text(f"{config.nr_hugepages}")
            if (entry / "nr_hugepages_mempolicy").exists():
                (entry / "nr_hugepages_mempolicy").write_text(f"{config.nr_hugepages_mempolicy}")
            if (entry / "nr_overcommit_hugepages").exists():
                (entry / "nr_overcommit_hugepages").write_text(f"{config.nr_overcommit_hugepages}")

        # Node specific configurations to "/sys/devices/system/node"
        root = Path(HUGETLB_NODE_SYSFS_PATH)
        while len(self.node_configs) > 0:
            config = self.node_configs.pop()
            entry = (root / f"node{config.node_id}" / \
                     f"hugepages" / f"hugepages-{config.hugepage_size}kB")
            if (entry / "nr_hugepages").exists():
                (entry / "nr_hugepages").write_text(f"{config.nr_hugepages}")
            # FIXME: val >= PMD_SIZE and val < config.hugepage_size
            # if (entry / "demote_size").exists():
            #    (entry / "demote_size").write_text(f"{config.demote_size}kB")

        # Indicator that the configurations have ben restored
        self.saved = False
