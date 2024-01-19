#!/bin/bash

# Generates .h files holding SPIR-V (encoded as uint32_t array) 
# from .vert/.frag/.comp glsl code.

for FILE in *.{comp,vert,frag}; do

    # In case there is no match, $FILE may still contain something like "*.comp"
    # in which case we want to skip this iteration of the loop.
    if [ ! -e "$FILE" ]; then
        continue
    fi

    FILENAME="${FILE%.*}_${FILE#*.}"
    VARNAME=$(echo "SPIRV_SOURCE_${FILENAME}" | tr '[:lower:]' '[:upper:]')

    echo "$FILE → ${FILENAME}.h"
    glslang -o "${FILENAME}.h" -V --vn "$VARNAME" "$FILE"
done
