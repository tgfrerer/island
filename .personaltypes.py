from dumper import *
from utils import TypeCode, DisplayFormat

# Qt Creator Debugging Helpers for glm vec2, vec3, vec4, mat3, and mat4 types

coordinate_names = ["x","y", "z", "w"]


def qdump__glm__vec(d, value):
    num_elements = ((value).type.size() // 4) # find out how many elements this vec has ()
    preview = ""
    for c in coordinate_names[:num_elements]:
        preview += '%s, ' % value[c].display()
    preview = preview[:-2] # remove last comma and whitespace
    d.putValue('{%s}' % preview )

    d.putNumChild(1)
    if d.isExpanded():
        with Children(d):
            for c in coordinate_names[:num_elements]:
                d.putSubItem(c, value[c])

# def qdump__glm__vec2(d, value):
#     d.putBetterType("glm::vec2")
#     qdump__glm__vec(d, value)

def qdump__glm__mat__col_type(d, value):
    d.putBetterType("glm::vec")
    qdump__glm__vec(d, value)
        
def qdump__glm__mat4(d, value):
    d.putValue("<4 columns>")
    d.putNumChild(1)
    if d.isExpanded():
        with Children(d, value):
            d.putSubItem("[0]",value[0][0])
            d.putSubItem("[1]",value[0][1])
            d.putSubItem("[2]",value[0][2])
            d.putSubItem("[3]",value[0][3])

def qdump__glm__mat3(d, value):
    d.putValue("<3 columns>")
    d.putNumChild(1)
    if d.isExpanded():
        with Children(d, value):
            d.putSubItem("[0]",value[0][0])
            d.putSubItem("[1]",value[0][1])
            d.putSubItem("[2]",value[0][2])


RESOURCE_TYPE_NAMES = {
    0: "Undefined",
    1: "Buffer",
    2: "Image",       
    3: "RtxBlas",       
    4: "RtxTlas",       
}

def decode_image_usage_flags(flags):
    if flags == 0:
        return ""
    names = []
    if flags & 0b01:
        names.append("eIsRoot")
    return "|".join(names)

def decode_buffer_usage_flags(flags):
    if flags == 0:
        return ""
    names = []
    if flags & 0b01:
        names.append("eIsVirtual")
    if flags & 0b10:
        names.append("eIsStaging")
    return "|".join(names)

def qdump__le_resource_handle(d, value):

    # value is a pointer → convert to integer
    raw = int(value.pointer())

    # Extract fields
    version      = (raw >> 0)  & 0x3F      # 6 bits
    usage_flags  = (raw >> 6)  & 0x03      # 2 bits
    type_id      = (raw >> 8)  & 0x0F      # 4 bits
    idx          = (raw >> 12) & 0xFFFFF   # 20 bits

    # num_samples is only meaningful for Image
    num_samples  = None
    
    if type_id == 2: # resource is an image
        num_samples = (raw >> 32) & 0x03
        usage_flags = decode_image_usage_flags(usage_flags)

    if type_id == 1:
        usage_flags = decode_buffer_usage_flags(usage_flags)

    # Human-friendly type name
    type_name = RESOURCE_TYPE_NAMES.get(type_id, f"Type({type_id})")

    summary = "[%6x |%3x]" % (idx, version)

    if usage_flags is not "":
        summary += f", usage={usage_flags}"

    if num_samples is not None:
        summary += f", samples={num_samples}"

    d.putBetterType("%s" % type_name)
    d.putValue(summary)

BINDLESS_RESOURCE_TYPE_NAMES = {
    0: "Undefined",
    1: "Texture",
    2: "Sampler",       
    3: "StorageImage",       
}

# dumper for the "bindless part" of a bindless resource handle
def qdump__le_bindless_sub_resource_handle(d, value):

    raw = int(value.pointer() >> 32) & 0xFFFFFFFF

    # Extract fields
    version      = (raw >> 0)  & 0xFF      # 6 bits
    type_id      = (raw >> 8)  & 0x0F      # 4 bits
    idx          = (raw >> 12) & 0xFFFFF   # 22 bits

    # Human-friendly type name
    type_name = BINDLESS_RESOURCE_TYPE_NAMES.get(type_id, f"Type({type_id})")

    # Compose summary string
    summary = (
        f"{type_name}"
        f" {idx},"
        f"v{version}"
    )

    d.putType("%s" % type_name)
    d.putValue("[%6x |%3x]" % (idx, version))

def qdump__le_image_resource_handle(d, value):
    d.putBetterType("le_resource_handle")
    qdump__le_resource_handle(d, value)

def qdump__le_buffer_resource_handle(d, value):
    d.putBetterType("le_resource_handle")
    qdump__le_resource_handle(d, value)

def splitHandle(handle):
    # handle is a gdb.Value containing a uint64_t
    value = int(handle)
    hi = (value >> 32) & 0xFFFFFFFF
    lo = value & 0xFFFFFFFF
    return lo, hi

# dumper for a bindless resource handle
def qdump__le_bindless_resource_handle(d, value):
    
    values = []
    values = splitHandle(value)
    
    names = ["bindless resource", "parent"]

    # lower bits contain the parent handle
    parent_el = d.createValueFromData(values[0],"le_resource_handle")

    # d.putValue("%x" % value.address)
    d.putValue("(0x%08x:%08x)" % (values[1], values[0]))
    d.putExpandable()

    num_children = 2 

    # if there is no parent handle, then we don't have to show it
    if values[0] == 0:
        num_children = 1

    d.putNumChild(num_children)

    if d.isExpanded():
        with Children(d):
            for i in range(0, num_children):
                with SubItem(d, None):
                    d.putName(names[i])
                    if i == 1:
                        # we can pretend that the item is a resource handle because
                        # the relevant bits (the lower 32 bits) are the same
                        qdump__le_resource_handle(d, parent_el)
                    else:
                        # the upper 32 bits are specific to bindless resources
                        qdump__le_bindless_sub_resource_handle(d, value)
                        # d.putValue("0%x" % values[1])

def qdump__le_bindless_sampler_handle(d, value):
    qdump__le_bindless_resource_handle(d, value)

def qdump__le_bindless_storage_image_handle(d, value):
    qdump__le_bindless_resource_handle(d, value)

def qdump__le_bindless_texture_handle(d, value):
    qdump__le_bindless_resource_handle(d, value)
