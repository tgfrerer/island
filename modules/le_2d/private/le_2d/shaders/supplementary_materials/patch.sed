# Insert StorageImageWriteWithoutFormat after OpCapability Shader
/^[[:space:]]*OpCapability Shader[[:space:]]*$/a\
               OpCapability StorageImageWriteWithoutFormat
# Replace OpTypeImage Rgba8 with Unknown, regardless of ID number
s/^\([[:space:]]*%[0-9]\+\)\([[:space:]]*=\s*OpTypeImage %float 2D 0 0 0 2 \)Rgba8/\1\2Unknown/

