#!/bin/sh -e

pushd vello_shaders

mkdir -p spv

for file in *.wgsl;do
	file_name=$(basename "$file" .wgsl)
	
	printf "processing: %s\n" "$file_name"

	rm -f "tmp_input.wgsl"

	tmpfile=$(mktemp)
	mv "$tmpfile" "tmp_input.wgsl"
	tmpfile="tmp_input.wgsl"

	if [ "$file_name" = "fine" ]; then 

		# Note that both fine files get turned into spirv-assembly files, not directly SPIRV-binaries
		# This is because we need to patch the generated spirv to allow unknown output image formats
		# Otherwise the output Image format is hardcoded to be rgba8.

		# fine with msaa defines is the 16 msaa permutation: we define 'msaa', and 'msaa16'
		sed -r 's/^#import\s(.*?).*/#include "shared\/\1.wgsl"/g' < "$file" | gcc -nostdinc -P -C -E -Dmsaa -Dmsaa16 - > $tmpfile
		tint --spirv-version 1.4 --format spvasm -o "spv/${file_name}_msaa16.spvasm" $tmpfile 

		# apply patch so that image output format does not need to be defined upfront
		sed -f ../patch.sed -i "spv/${file_name}_msaa16.main.spvasm"
		echo "	patched spv/${file_name}_msaa16.main.spvasm"

		spirv-as "spv/${file_name}_msaa16.main.spvasm" --target-env vulkan1.4 --preserve-numeric-ids -o "spv/${file_name}_msaa16.main.spv"
		echo "	assembled spv/${file_name}_msaa16.main.spv"
		
		# fine without any defines is area shader
		sed -r 's/^#import\s(.*?).*/#include "shared\/\1.wgsl"/g' < "$file" | gcc -nostdinc -P -C -E - > $tmpfile 
		tint --spirv-version 1.4 --format spvasm -o "spv/${file_name}_area.spvasm" $tmpfile 
		
		# apply patch so that image output format does not need to be defined upfront
		sed -f ../patch.sed -i "spv/${file_name}_area.main.spvasm"
		echo "	patched spv/${file_name}_msaa16.main.spvasm"

		spirv-as "spv/${file_name}_area.main.spvasm" --target-env vulkan1.4 --preserve-numeric-ids -o "spv/${file_name}_area.main.spv"
		echo "	assembled spv/${file_name}_msaa16.main.spv"

	elif [ "$file_name" = "pathtag_scan" ]; then 
		# pathtag without any defines is the large pathtag scan
		sed -r 's/^#import\s(.*?).*/#include "shared\/\1.wgsl"/g' < "$file" | gcc -nostdinc -P -C -E - > $tmpfile 
		tint --spirv-version 1.4 --format spirv -o "spv/${file_name}_large.spv" $tmpfile  
		
		# pathtag with define is the small pathtag scan: we define 'small'
		sed -r 's/^#import\s(.*?).*/#include "shared\/\1.wgsl"/g' < "$file" | gcc -nostdinc -P -C -E -Dsmall - > $tmpfile 
		tint --spirv-version 1.4 --format spirv -o "spv/${file_name}_small.spv" $tmpfile  
		# naga --stdin-file-path - -g --input-kind wgsl "spv/${file_name}_small.spv"
	else
		sed -r 's/^#import\s(.*?).*/#include "shared\/\1.wgsl"/g' < "$file" | gcc -nostdinc -P -C -E - > $tmpfile 
		tint --spirv-version 1.4 --format spirv -o "spv/${file_name}.spv" $tmpfile  
	fi

	rm $tmpfile

	# tint does not respect the output filename -- you must remove `.main` from the name of all the spv files that were generated.

done

pushd spv
	
for file in *.spv; do
	filename=$(basename "$file" .main.spv)
	cp $file "../../../vello/${filename}.spv"
	echo "copied ../../../vello/${filename}.spv"
done

popd

# convert .spv files to .inl files
pushd "../../vello/"
	./compress_shaders.sh
popd 