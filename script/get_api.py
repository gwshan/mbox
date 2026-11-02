#
#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Get public APIs from the specified C source file
#
# Copyright (c) 2026 Red Hat, Inc.
#
# Author: Gavin Shan <gshan@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
#

import argparse
import re
import sys

from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description="Get public available functions")
    parser.add_argument("--file", help="source file")
    functions = []

    # Validate the input arguments
    args = parser.parse_args()
    if args.file is None:
        print(f"ERROR: Source file is needed by '--file' option")
        sys.exit(1)

    # Open the source file
    file_path = Path(f"{args.file}")
    try:
        with open(file_path.expanduser(), "r") as f:
            content = f.read()
    except:
        print(f"ERROR: Unable to open source file <{args.file}>")
        sys.exit(1)

    # Process file's contents
    lines = content.split('\n')
    i = 0
    while i < len(lines):
        line = lines[i]

        # Skip static functions, preprocessor, comments, blank lines
        if (line.startswith('static ') or line.startswith('#') or
            line.startswith('/*') or line.startswith(' *') or
            line.startswith('//') or line.strip() == ''):
            i += 1
            continue

        # Skip lines that are clearly not function definitions
        if (line.startswith('}') or line.startswith('{') or
            line.startswith(' ') or line.startswith('\t')):
            i += 1
            continue

        # Combine multi-line declarations (up to 10 lines until '{')
        combined = line
        j = i + 1
        while j < len(lines) and '{' not in combined and j - i < 10:
            combined += ' ' + lines[j]
            j += 1

        # Match function definition: type name(params) {
        match = re.match(r'^[\w][\w\s\*]*?\b(\w+)\s*\(', combined)
        if match and '{' in combined:
             func = match.group(1)
             skip = ('if', 'while', 'for', 'switch', 'return',
                     'struct', 'typedef', 'enum',
                     'QTAILQ_HEAD', 'DECLARE_OBJ_CHECKERS')
             if (func not in skip and 
                 not combined.strip().startswith('typedef') and
                 not combined.strip().startswith('struct ')):
                 functions.append(func)

        i += 1

    # Print the APIs
    for func in sorted(set(functions)):
        print(func) 

if __name__ == "__main__":
    main()
