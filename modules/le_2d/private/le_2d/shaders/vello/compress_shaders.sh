#!/bin/sh -e

# if the compressor binary does not yet exist, we must compile it
#
if [ ! -e compress ]; then
	g++ binary_to_compressed_c.cpp -o compress
fi

# we should now have a compressor that we can use 
mkdir -p "../../inl"

for FILE in *.spv; do
	echo "$FILE"
	FILENAME=$(basename "$FILE" .spv)
	./compress -base85 "$FILE" "$FILENAME" > "../../inl/$FILENAME.inl"
done
