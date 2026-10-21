#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Utility library 
#
# Copyright (c) 2026 Red Hat, Inc.
#
# Author: Gavin Shan <gshan@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
#

import os
import re
from dataclasses import dataclass, field
from pathlib import Path

class Util:
    def string_to_size(string: str) -> int:
        multipliers = {
            'b': 1,       'k': 1024,
            'm': 1024**2, 'g': 1024**3,
            't': 1024**4, 'p': 1024**5
        }

        match = re.search(r"(\d+(?:.)?)", string, re.IGNORECASE)
        if not match:
            return 0

        # No unit at tail
        substring = match.group(1).lower()
        if substring[-1].isdigit():
           return int(substring)

        # Invalid unit at tail
        if substring[-1] not in multipliers:
           return int(substring[:-1])

        return int(int(substring[:-1]) * multipliers[substring[-1]])
