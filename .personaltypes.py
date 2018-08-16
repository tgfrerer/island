from dumper import *

# Qt Creator Debuggin Helpers for glm vec3,vec4,mat3, and mat4 types

def qdump__glm__vec3(d, value):
    d.putValue('[%s, %s, %s]' % (value["x"].display(), value["y"].display(), value["z"].display()))
    d.putNumChild(1)
    if d.isExpanded():
        with Children(d):
            d.putSubItem("x", value["x"])
            d.putSubItem("y", value["y"])
            d.putSubItem("z", value["z"])
        
def qdump__glm__vec4(d, value):
    d.putValue('[%s, %s, %s, %s]' % (value["x"].display(), value["y"].display(), value["z"].display(), value["w"].display()))
    d.putNumChild(1)
    if d.isExpanded():
        with Children(d):
            d.putSubItem("x", value["x"])
            d.putSubItem("y", value["y"])
            d.putSubItem("z", value["z"])
            d.putSubItem("w", value["w"])

def qdump__glm__mat__col_type(d, value):
    vecSize = (value).type.size() / 4
    if   vecSize == 4:
        d.putBetterType("glm::vec4")
        qdump__glm__vec4(d, value)
    elif vecSize == 3:
        d.putBetterType("glm::vec3")
        qdump__glm__vec3(d, value)
    else:
        pass
def qdump__glm__mat4(d, value):
    d.putValue("%s" % value.type.name)
    d.putNumChild(1)
    if d.isExpanded():
        with Children(d, value):
            d.putSubItem("[0]",value[0][0])
            d.putSubItem("[1]",value[0][1])
            d.putSubItem("[2]",value[0][2])
            d.putSubItem("[3]",value[0][3])

def qdump__glm__mat3(d, value):
    d.putValue("%s" % value.type.name)
    d.putNumChild(1)
    if d.isExpanded():
        with Children(d, value):
            d.putSubItem("[0]",value[0][0])
            d.putSubItem("[1]",value[0][1])
            d.putSubItem("[2]",value[0][2])

