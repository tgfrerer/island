#!/bin/bash

# list apps you want to test

apps_list=("
	examples/hello_world:Island-HelloWorld
	examples/hello_triangle:Island-HelloTriangle
	examples/lut_grading_example:Island-LutGradingExample
	examples/multi_window_example:Island-MultiWindowExample
	examples/imgui_example:Island-ImguiExample		
")

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

	mkdir -p $build_dir
	pushd $build_dir
	
	cmake --config ../.. -DCMAKE_BUILD_TYPE=Release -GNinja
	
	# we store the timestamp of the last built app executable
	local previous_hash=0
	local default_hash=0
	default_hash=`tar cf - modules ${app_name} | md5sum`
	
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
		popd 
		return $return_code
	else
		# ninja did okay.
		# we must now check if we have a new artifact, or if its still the same.

		local current_hash=`tar cf - modules ${app_name} | md5sum`
		popd

		if [[ $current_hash != $previous_hash ]];
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
	local app_dir=${app_names[0]}
	local app_name=${app_names[1]}
	local app_base_dir="$FILE_DIR/../../apps/$app_dir"

	if [ -d $app_base_dir ] 
	then
		local build_dir="${app_base_dir}/build/Desktop-Test"

		build_app $build_dir $app_name &>build.log
		local build_result=$?

		# echo "BUILD result: ${build_result}" 

		if test $build_result -eq 2 
		then
			echo -n "[  ==  ]       : ${app_name}"
			return 0 
		elif test $build_result -ne 0 
			then
			echo -n "[ FAIL ] Build : ${app_name}"
			echo "--------------" >> build.err
			echo "${app_name} BUILD FAILED: " >> build.err
			echo "--------------" >> build.err
			cat build.log >> build.err
            cat build.err
			return 1
		fi

		echo -n "[  OK  ] Build : ${app_name}"

	else 
		echo "directory not found: '${app_base_dir}'"
		exit 1
	fi
}

# main script

IFS=$'\n' read -ra arr -d '' <<<"$apps_list"

# Set error checking trap - this will cause the script to fail 
# at the first non-zero exit code it encounters. 
# We can only set the trap here, as the `read` call above would
# otherwise trigger it prematurely.
#
# trap 'error ${LINENO}' ERR

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


for a in "${arr[@]}"; do 
	b=`echo $a | tr -d [:space:]`; 
	process_app $b
done
