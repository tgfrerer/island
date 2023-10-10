#!/usr/bin/python3

import sys
import argparse

try:
    import gnureadline as readline
except ImportError:
    import readline

parser = argparse.ArgumentParser(description='Generate Vulkan struct templates based on user input.')
parser.add_argument("--vk_path", default="/usr/share/vulkan/registry", help='absolute path to vulkan registry')

args = parser.parse_args()

vk_registry_path = args.vk_path

# we want to have access to all python modules from `vk_registry_path`
sys.path.append(vk_registry_path)

from reg import Registry

reg = Registry()
reg.loadFile(vk_registry_path + "/vk.xml")

def generate_struct(struct_name):

    type_info = reg.typedict.get(struct_name)

    if type_info is None:
        print("Key `{}` not found.".format(struct_name))
        return ""

    members = type_info.elem.findall('.//member')

    body = "{} = {{\n".format(struct_name)

    longest_name_len = 0
    for member in members:
        for elem in member:
            if elem.tag == 'name':
                name_len = len(elem.text)
                if longest_name_len < name_len:
                    longest_name_len = name_len

    for member in members:
        member_name = ""
        member_value = member.get('values') or '0'
        member_optional = member.get('optional') == 'true'
        for elem in member:
            if elem.tag == 'name':
                member_name = elem.text
            if elem.tag == 'type' and elem.text == 'void':
                member_value = "nullptr"

        if member_optional:
            txt = "\t.{: <" + str(longest_name_len + 2) + "} = {}, // optional\n"
        else:
            txt = "\t.{: <" + str(longest_name_len + 2) + "} = {}, \n"
        # print(txt)
        body += txt.format(member_name, member_value)
        # ipdb.set_trace()

    body += "};\n"

    return body


def str2bool(v) -> bool:
    """evaluate anything that is not true(ish) as false"""
    return str(v).lower() in ("yes", "true", "t", "1")

print("// Enter a Vulkan struct name to begin, Ctl+D to end interactive session:")


while True:
    body = ""
    try:
        line = input("('q' to quit): ")
    except EOFError:
        exit(0)
    line = line.split("#")[0]  # remove any comment lines
    if line in ["q", "quit", "exit"]:
        break
    else:
        # only continue if there is anything left of the line to process (comment lines will be empty at this point)
        params = line.split(",")
        for i, p in enumerate(params):
            # remove any extra whitespace around our input
            params[i] = p.strip()
        if len(params) > 0 and params[0]:
            body += "\n// " + "-" * 70 + "\n\n"
            body += generate_struct(params[0])

        body += "\n// " + "-" * 70 + "\n\n"

    print(body)
