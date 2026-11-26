#!/usr/bin/env python3
"""Fail when linked code makes a plain lcall/ljmp into another code bank,
or when the console output path makes any banked call.

A function in a bank is only reachable from a different bank through a
__banked call; a plain call lands in whatever bank is mapped at the time.
The console output path must stay in the common area: the flash code prints
while the flash is in SIO mode, when no bank is readable.
Host tests cannot see either, so check the linker output instead.
Usage: check_bank_calls.py <build dir>
"""
import glob
import os
import re
import sys

build = sys.argv[1]
syms = {}
with open(os.path.join(build, "rtlplayground.map")) as f:
    for line in f:
        m = re.match(r"\s*C:\s+([0-9A-F]{8})\s+(\S+)", line)
        if m:
            syms[m.group(2)] = int(m.group(1), 16)

call = re.compile(r"\s+([0-9A-F]{6})\s+(?:[0-9A-F]{2} )+\s*\[\d+\]\s+\d+\s+(lcall|ljmp)\s+(_\w+)")
bad = 0
for rst in sorted(glob.glob(os.path.join(build, "**", "*.rst"), recursive=True)):
    with open(rst, errors="replace") as f:
        for line in f:
            m = call.match(line)
            if not m or m.group(3) not in syms:
                continue
            at, target = int(m.group(1), 16), syms[m.group(3)]
            if target >> 16 and target >> 16 != at >> 16:
                print("%s: %s at 0x%05x calls %s in bank %d" % (
                    os.path.relpath(rst, build), m.group(2), at, m.group(3), target >> 16 & 0xf), file=sys.stderr)
                bad += 1
# Functions that run with the flash in SIO mode, through print_string()
SIO_SAFE = {"_write_char", "_write_char_no_syslog"}
func = re.compile(r"\s+[0-9A-F]{6}\s+\d+\s+(_\w+):$")
for rst in sorted(glob.glob(os.path.join(build, "**", "*.rst"), recursive=True)):
    cur = None
    with open(rst, errors="replace") as f:
        for line in f:
            m = func.match(line)
            if m:
                cur = m.group(1)
            elif cur in SIO_SAFE and "__sdcc_banked_call" in line:
                print("%s: %s makes a banked call; it runs while the flash is in SIO mode" % (
                    os.path.relpath(rst, build), cur), file=sys.stderr)
                bad += 1

if bad:
    print("%d banking problem(s); see above" % bad, file=sys.stderr)
    sys.exit(1)
