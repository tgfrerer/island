#!/usr/bin/python3

import sys
import argparse
import ipdb

parser = argparse.ArgumentParser(description='Generate enum code mirroring given vulkan enums')
parser.add_argument("--vk_path", default="/usr/share/vulkan/registry", help='absolute path to vulkan registry')

args=parser.parse_args()

vk_registry_path = args.vk_path

# we want to have access to all python modules from `vk_registry_path`
sys.path.append(vk_registry_path)

from reg import Registry
from generator import OutputGenerator, GeneratorOptions, write

reg = Registry()
reg.loadFile(vk_registry_path + "/vk.xml")

og = OutputGenerator()
og.diagFile = open("/tmp/diagfile.txt", "w")

import re

def atoi(text):
    # we convert every enum value to a textual representation. For numbers, we
    # make sure that the value is a 16 digit hex representation. By using this
    # uniform representation, we ensure human sorting, i.e. 0x03 comes before 0x10,
    # whereas otherwise there was a risk of `0x10` being alphabetically sorted
    # ahead of `0x3` 
    return format(int(text),'016x') if text.isdigit() else text

def to_titled_camel_case(snake_str):
	components = snake_str.split('_')
	# We capitalize the first letter of each component.
    # for the terms: 1D, 2D, 3D, 4D number-and-letter combination
    # python's `title` method does the right thing automatically
    # but we might want to preserve capitalisation for anything NV,
	return ''.join(x.title() for x in components)

def friendly_enum_value_name(enum_name, prefix):
    return to_titled_camel_case(enum_name).removeprefix(prefix).removesuffix('Bit')

def generate_enum_group(original_enum_name, wants_to_string=False, num_bits=32):

    enum_name = '%s' % original_enum_name.removeprefix('Vk')
    enum_value_prefix = original_enum_name.split('FlagBits')[0]
    if original_enum_name.find('FlagBits2') > 0:
        enum_value_prefix = enum_value_prefix + '2'
    
    group_info = reg.groupdict[original_enum_name]
    enum_type = group_info.elem.get('type')
    num_bits = int(group_info.elem.get('bitwidth') or 32)
    
    body = ""
   
    is_bitmask = (enum_type == 'bitmask')

    if is_bitmask:
        # in case we have a bitmask enum, we must define a type which may
        # contain the flag bits - we do this for type safety
        bitmask_type = enum_name.replace("FlagBits", "Flags") 
        body += "using %s = uint%s_t;\n" % (bitmask_type, num_bits)
        body += "enum class %s : %s {\n" % (enum_name, bitmask_type ) 
    else:
        body += "enum class %s : uint%s_t {\n" % (enum_name, num_bits ) 
    
    enums = group_info.elem.findall('enum')

    # we need to clean enums - there is a chance of duplicate names- we must get rid of these
    enums = og.checkDuplicateEnums(enums)

    # we sort our enum values by value, because aliases require to have their
    # target defined before themselves - we can enforce that by having anything
    # numeric listed earlier
    enums.sort(key=lambda e: atoi(og.enumToValue(e,True,num_bits)[1]))

    # ipdb.set_trace()
    # iterate over all values
    for e in enums:
        e_val = og.enumToValue(e, True, num_bits)

        # we must test whether the value is an alias, or a numerical value
        if e_val[0] == None and e_val[1] != 0:
            # value is an alias
            e_val = "e%s" % friendly_enum_value_name(e_val[1],enum_value_prefix)
        else:
            # value is a numerical value
            e_val = e_val[1]

        body += ("e%s = %s,"  % (friendly_enum_value_name(e.get('name'),enum_value_prefix), e_val))
        if e.get('comment') : 
            body += "// %s" % e.get('comment')
        body += "\n"
    
    body += "};\n\n"
   
    if is_bitmask:
        # implement the | or operator for our new class enum
        body += """constexpr {bm_tp} operator | ({tp} const & lhs, {tp} const & rhs) noexcept {{
        return static_cast<const {bm_tp}>(static_cast<{bm_tp}>(lhs) | static_cast<{bm_tp}>(rhs));
        }};\n\n""".format(bm_tp=bitmask_type, tp=enum_name)
        # implement the chained | or operator for our new class enum
        body += """constexpr {bm_tp} operator | ({bm_tp} const & lhs, {tp} const & rhs) noexcept {{
        return static_cast<const {bm_tp}>(lhs | static_cast<{bm_tp}>(rhs));
        }};\n\n""".format(bm_tp=bitmask_type, tp=enum_name)
    
        # implement the & or operator for our new class enum
        body += """constexpr {bm_tp} operator & ({tp} const & lhs, {tp} const & rhs) noexcept {{
        return static_cast<const {bm_tp}>(static_cast<{bm_tp}>(lhs) & static_cast<{bm_tp}>(rhs));
        }};\n\n""".format(bm_tp=bitmask_type, tp=enum_name)
    
    if wants_to_string:
        # iterate over all values
        body += "static constexpr char const * to_str(const {tp}& tp){{\n".format(tp=enum_name)
        body += "\tswitch( static_cast<uint%s_t>(tp)){\n" % num_bits
        body += "// clang-format off\n"
        for e in enums:
            e_val = og.enumToValue(e, True, num_bits)
            if e_val[0] != None:
                e_val = e_val[1]
                body += ("\t\tcase {val}: return \"{name}\";\n".format(name=friendly_enum_value_name(e.get('name'),enum_value_prefix), val=e_val.rjust(10)))

        body += ("\t\tdefault: return \"Unknown\";\n");
        body += "\t\t// clang-format on\n"
        body += "\t};\n"
        body += "}\n\n"

    return body


def str2bool(v) -> bool:
    """evaluate anything that is not true(ish) as false"""
    return str(v).lower() in ("yes", "true", "t", "1")


body = """#pragma once

// This file was auto-generated using gen_le_enums.py

#include <stdint.h>

namespace le {
"""

for line in sys.stdin:
    line = line.split("#")[0] # remove any comment lines
    if line:
        # only continue if there is anything left of the line to process (comment lines will be empty at this point)
        params = line.split(",")
        for i,p in enumerate(params):
            # remove any extra whitespace around our input
            params[i] = p.strip()
        if len(params)>0 and params[0]:
            body += "\n// " + "-" * 70 + "\n\n"
            body += generate_enum_group(params[0], wants_to_string=(len(params)>1 and str2bool(params[1])))


body += "\n// " + "-" * 70 + "\n\n"
body += "} // end namespace le \n"

print(body)
