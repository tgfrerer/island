#!/usr/bin/python3

import re

# todo: fetch number of elements from le_core.cpp file: it is the value associated with CORE_MAX_CALLBACK_FORWARDERS

pattern = re.compile(r"\s*?#define\s+?CORE_MAX_CALLBACK_FORWARDERS\s+?(\d+).*")

core_max_callback_forwarders = 0

with open("le_core.cpp", "rt") as core_file:
	for line in core_file:
		result = pattern.search(line)
		if result is not None:
			core_max_callback_forwarders = int(result[1])
			break

if (core_max_callback_forwarders == 0):
	exit(1)

print("asm(R\"ASM(\n")


for offset in range(0, core_max_callback_forwarders):
	print("\tpush %rbx")
	print("\tmovq  $0x{:04x}, %rbx".format(offset * 8 ))
	print("\tjmp sled_end\n")

print("sled_end:")
print(")ASM\");")
