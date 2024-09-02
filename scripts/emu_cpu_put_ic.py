#!/bin/python3
import re
import sys

if len(sys.argv) < 2:
    print("./emu_cpu_put_ic.py decode-insns.c.inc")
    exit(0)

f = open(sys.argv[1], "r")
lines = list(f.readlines())

m = {}

for line in lines:
    if line.startswith("typedef arg_"):
        linesp = line[:-2].split()
        m[linesp[2]] = linesp[1]

# print(m)

inst  = set()

body = []

for line in lines:
    r = re.search("if \(trans_(.*)\(ctx, (.*)\)\)", line)
    if r:
        # print(r.span()[0])
        # print(r.group(0), r.group(1), r.group(2), sep="===")
        # print(r.group(2)[5:])
        # s = "if (handle_arg_" + r.group(2)[5:] + "(ctx, " + r.group(2) + ", LA_INST_" + r.group(1).upper() + "))"
        add_line = "cpu_put_ic(ctx, (bool (*)(void*, void*))trans_%s, %s, insn);" % (r.group(1), r.group(2))
        print(add_line)
        # line = line[0:r.span()[0]] + s + line[r.span()[1]:]
        # inst.add("LA_INST_" + r.group(1).upper())
    print(line, end="")
