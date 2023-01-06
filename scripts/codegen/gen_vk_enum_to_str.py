#!/usr/bin/python3

import sys
import argparse
import re

parser = argparse.ArgumentParser(description='Generate enum code mirroring given vulkan enums')
parser.add_argument("--vk_path", default="/usr/share/vulkan/registry", help='absolute path to vulkan registry')

args = parser.parse_args()

vk_registry_path = args.vk_path

# we want to have access to all python modules from `vk_registry_path`
sys.path.append(vk_registry_path)

from reg import Registry
from generator import OutputGenerator, GeneratorOptions, write
from apiconventions import APIConventions

reg = Registry()
reg.loadFile(vk_registry_path + "/vk.xml")

og = OutputGenerator()
og.diagFile = open("/tmp/diagfile.txt", "w")
og.genOpts = GeneratorOptions(conventions=APIConventions, apiname=APIConventions().xml_api_name)


def atoi(text):
    # we convert every enum value to a textual representation. For numbers, we
    # make sure that the value is a 16 digit hex representation. By using this
    # uniform representation, we ensure human sorting, i.e. 0x03 comes before 0x10,
    # whereas otherwise there was a risk of `0x10` being alphabetically sorted
    # ahead of `0x3`
    return format(int(text), '016x') if text.isdigit() else text


def camel_to_snake(name):
    name = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', name).lower()


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

    group_info = reg.groupdict.get(original_enum_name)
    if group_info is None:
        return "// Not found: {}".format(original_enum_name)

    enum_type = group_info.elem.get('type')

    is_bitmask = (enum_type == 'bitmask')

    num_bits = int(group_info.elem.get('bitwidth') or 32)

    body = ""

    enums = group_info.elem.findall('enum')

    # we need to clean enums - there is a chance of duplicate names- we must get rid of these
    enums = og.checkDuplicateEnums(enums)

    # we sort our enum values by value, because aliases require to have their
    # target defined before themselves - we can enforce that by having anything
    # numeric listed earlier
    enums.sort(key=lambda e: atoi(og.enumToValue(e, True, num_bits)[1]))

    # iterate over all values
    body += "static constexpr char const * to_str_{to_bits_name}(const {tp}& tp){{\n".format(
        tp=original_enum_name, to_bits_name=camel_to_snake(original_enum_name))
    body += "\tswitch( static_cast<int%s_t>(tp)){\n" % num_bits
    body += "// clang-format off\n"

    for e in enums:
        e_val = og.enumToValue(e, True, num_bits)
        if e_val[0] is not None:
            format_str = ("\t     case {val: <10}: return \"{name}\";\n")
            body += format_str.format(name=friendly_enum_value_name(
                e.get('name'), enum_value_prefix), val=e_val[1])

    body += ("\t          {: <"+str(10 + 2) +
             "}: return {};\n").format("default", "\"\"")
    body += "\t// clang-format on\n"
    body += "\t};\n"
    body += "}\n\n"

    if is_bitmask:
        # in case we have a bitmask enum, we must define a type which may
        # contain the flag bits - we do this for type safety
        flags_name = original_enum_name.replace("FlagBits", "Flags")
        body += """static std::string to_string_{method_name}( const {flags_name}& tp ) {{
	uint64_t flags = tp;
	std::string result;
	int bit_pos = 0;
	while ( flags ) {{
		if ( flags & 1 ) {{
			if ( false == result.empty() ) {{
				result.append( " | " );
            }}
			result.append( to_str_{to_bits_name}( {bits_name}( 1ULL << bit_pos ) ) );
        }}
		flags >>= 1;
		bit_pos++;
    }}
	return result;
}}""".format(flags_name=flags_name, bits_name=original_enum_name, to_bits_name=camel_to_snake(original_enum_name), method_name=camel_to_snake(flags_name))

    return body


def str2bool(v) -> bool:
    """evaluate anything that is not true(ish) as false"""
    return str(v).lower() in ("yes", "true", "t", "1")


body = """#pragma once
//
// *** THIS FILE WAS AUTO-GENERATED - DO NOT EDIT ***
//
// See ./scripts/codegen/gen_vk_enum_to_str.py for details.
//

#include <stdint.h>

"""

print(body)

for line in sys.stdin:
    line = line.split("#")[0]  # remove any comment lines
    if line:
        body = ""
        # only continue if there is anything left of the line to process (comment lines will be empty at this point)
        params = line.split(",")
        for i, p in enumerate(params):
            # remove any extra whitespace around our input
            params[i] = p.strip()
        if len(params) > 0 and params[0]:
            body += "\n// " + "-" * 70 + "\n\n"
            body += generate_enum_group(params[0], wants_to_string=(
                len(params) > 1 and str2bool(params[1])))
        print(body)


body = ""
body += "\n// " + "-" * 70 + "\n\n"

print(body)
