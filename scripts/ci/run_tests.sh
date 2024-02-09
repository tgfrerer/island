#!/bin/bash

FILE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# Ingest the list of apps that we want to test into an array: apps_list
mapfile -t apps_list < "${FILE_DIR}/tests.txt"

tempfiles=( )
cleanup() {
  rm -f "${tempfiles[@]}"
}
trap cleanup 0

error() {
  local parent_lineno="$1"
  local message="$2"
  local code="${3:-1}"
  if [[ -n "$message" ]] ; then
    echo "Error on or near line ${parent_lineno}: ${message}; exiting with status ${code}"
  else
    echo "Error on or near line ${parent_lineno}; exiting with status ${code}"
  fi
  exit "${code}"
}

# build app using ninja - create build directory if it does not exist yet
build_app(){
	local build_dir=$1
	local app_name=$2
    local build_type=$3

	mkdir -p "$build_dir"
	pushd "$build_dir" || exit
	
	cmake ../.. -DCMAKE_BUILD_TYPE="${build_type}" -GNinja
	
	# we store the timestamp of the last built app executable
	local previous_hash=0
	local default_hash=0
    default_hash=$(tar cf - modules "${app_name}" | md5sum)
	
	# if no previous executable exists, we must still set the 
	# previous_hash to something sensible
	if test $? -eq 0 
	then
		previous_hash=$default_hash
	else
		echo "could not stat"
	fi

	ninja

	local return_code=$?

	if test $return_code -ne 0
	then
		# something went wrong with compilation
		popd || exit
		return $return_code
	else
		# ninja did okay.
		# we must now check if we have a new artifact, or if its still the same.

        local current_hash=$(tar cf - modules "${app_name}" | md5sum)
		popd || exit

		if [[ $current_hash != "$previous_hash" ]];
		then
			# we have a new artifact
			echo "time mismatch: $current_hash != $previous_hash"  
			return 0
		else
			echo "$current_hash == $previous_hash"
			# artifact has not changed.
			echo "no change" 
			return 2
		fi

	fi
}

process_app(){
	FILE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
	IFS=: read -ra app_names -d '' <<<"$1"
    local build_type="$2"
    local app_dir=$(echo ${app_names[0]} | xargs)
    local app_name=$(echo ${app_names[1]} | xargs)
	local app_base_dir="$FILE_DIR/../../apps/$app_dir"

    # printf "'%s' '%s' '%s'\n" "${app_dir}" "${app_name}" "${app_base_dir}"

	if [ -d "$app_base_dir" ] 
	then
		local build_dir="${app_base_dir}/build/Desktop-Test_${build_type}"

		build_app "${build_dir} ${app_name} ${build_type}" &>build.log
		local build_result=$?

		# echo "BUILD result: ${build_result}" 

		if test $build_result -eq 2 
		then
			printf "[  ==  ] %- 10s: %s\n" "${build_type}" "${app_name}"
			return 0 
		elif test $build_result -ne 0 
			then
			printf "[ FAIL ] %- 10s: %s\n" "${build_type}" "${app_name}"
			echo "--------------" >> build.err
			echo "${build_type} ${app_name} BUILD FAILED: " >> build.err
			echo "--------------" >> build.err
			cat build.log >> build.err
            cat build.err
			return 1
		fi

		printf "[  OK  ] %- 10s: %s\n" "${build_type}" "${app_name}"

	else 
		echo "directory not found: '${app_base_dir}'"
		exit 1
	fi
}

# main script

# Set error checking trap - this will cause the script to fail 
# at the first non-zero exit code it encounters. 
# We can only set the trap here, as the `read` call above would
# otherwise trigger it prematurely.
#
trap 'error ${LINENO}' ERR

if [ -f build.err ] 
then
	rm build.err
fi

if [ -f run.err ] 
then
	rm run.err
fi

# make sure to update all git submodules 

git submodule init
git submodule update --depth=1


for a in "${apps_list[@]}"; do 
    b=$(echo "$a" | tr -d "[:space:]"); 
	process_app "$b" Debug
	process_app "$b" Release
done
