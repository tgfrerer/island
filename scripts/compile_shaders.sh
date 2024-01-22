#!/bin/bash

# This script will compile shaders from glsl source to spir-v, 
# and will autogenerate a .h file with embedded spir-v code for 
# each source shader  file.

INVOCATION_DIR=$(realpath "$PWD")
WORKING_DIR="$INVOCATION_DIR"

if [ "$1" ]; then
    # echo "Parameter given: '$1'"
    if [[ -d "${WORKING_DIR}/$1" ]]; then
        # echo "parameter names a directory"
        WORKING_DIR="${WORKING_DIR}/$1"
    elif [[ -f "${WORKING_DIR}/$1" ]]; then
        # echo "parameter does name a file"
        TARGET_FILE="${WORKING_DIR}/$1"
        WORKING_DIR=$(dirname "$TARGET_FILE")
    else
        echo "File or directory not found: '$1'"
        exit 1
    fi
fi

WORKING_DIR=$(realpath "${WORKING_DIR}")

cd "$WORKING_DIR" || exit 1

# echo "working dir set to: '$WORKING_DIR'"
# echo "target file set to $TARGET_FILE"

######################################################################
# Generates .h files holding SPIR-V (encoded as uint32_t array) 
# from .vert/.frag/.comp glsl code.
######################################################################
if [[ -e "$TARGET_FILE" ]]; then
    # echo "target file exists"
    FILES="$TARGET_FILE"
else
    FILES=$(find "$WORKING_DIR" \
           -name "*.vert" \
        -o -name "*.frag" \
        -o -name "*.comp" \
    )
fi

for FILE in $FILES; do

    # In case there is no match, $FILE may still contain something like "*.comp"
    # in which case we want to skip this iteration of the loop.
    if [ ! -e "$FILE" ]; then
        continue
    fi

    FILENAME=$(basename "${FILE%.*}_${FILE#*.}")
    VARNAME=$(echo "SPIRV_SOURCE_${FILENAME}" | tr '[:lower:]' '[:upper:]')

    echo "$FILE → ${FILENAME}.h"
    glslang --quiet -o "${FILENAME}.h" -V --vn "$VARNAME" "$FILE" 
done

cd "$INVOCATION_DIR" || exit 1
