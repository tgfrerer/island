#!/bin/bash

# list apps you want to test

apps_list=("
	test_ply:Island-TestPly
	blob_wave:Island-BlobWave
	aeon:Island-Aeon
	bb_spectrum:Island-BbSpectrum
	fourier_blob:Island-FourierBlob
	langtons_loops:Island-LangtonsLoops
	physarum:Island-Physarum
	quad_bezier:Island-QuadBezier
	show_font:Island-ShowFont
	test_blob:Island-TestBlob
	test_blob_refraction:Island-TestBlobRefraction
	test_bloom:Island-TestBloom
	test_compute:Island-TestCompute
	test_dependency_tracker:Test_Dependency_Tracker
	test_depth_draw:Island-TestDepthDraw
	test_ecs:Island-TestEcs
	test_font:Island-TestFont
	test_font_path:Island-TestFontPath
	test_font_refraction:Island-TestFontRefraction
	test_glsl_include:Island-TestGlslInclude
	test_img_attachment:Island-TestImgAttachment
	test_img_swapchain:Island-TestImgSwapchain
	test_mesh_generator:Island-TestMeshGenerator
	test_mipmaps:Island-TestMipMaps
	test_multisample:Island-TestMultisample
	test_outline:Island-TestOutline
	test_paintbrush:Island-TestPaintbrush
	test_param:Island-TestParam
	test_parameter:Island-TestParameter
	test_path:Island-TestPath
	test_path_render:Island-TestPathRender
	test_ping_pong:Island-TestPingPong
	test_ply:Island-TestPly
	test_poisson_blur:Island-TestPoissonBlur
	test_resolve:Island-TestResolve
	test_sdf:Island-TestSdf
	test_setup_pass:Island-TestSetupPass
	test_trails:Island-TestTrails
	test_turntable:Island-TestTurntable
	test_verlet:Island-TestVerlet
	workbench:Island-WorkbenchApp
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

# main methods 

screenshot_exists(){
	local build_dir=$1
	local app_name=$2
	local screenshot_dir="./screenshots"

	if [ -f "${screenshot_dir}/${app_name}.png" ]
	then
		return 1 
	else
		return 0
	fi
}

copy_screenshot(){
	local build_dir=$1
	local app_name=$2
	local screenshot_dir="./screenshots"

	mkdir -p $screenshot_dir
	convert ${build_dir}/60.ppm  ${screenshot_dir}/${app_name}.png
}

# run app and takes screenshot using the vulkan screenshot layer
run_app(){
	local build_dir=$1
	local app_name=$2

	pushd $build_dir
	env VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES="60" ./${app_name} &
	local app_pid=$! 
	
	sleep 4
	kill -s TERM $app_pid
	local return_code=$?
	popd
	return $return_code
}

# build app using ninja - create build directory if it does not exist yet
build_app(){
	local build_dir=$1
	local app_name=$2

	mkdir -p $build_dir
	pushd $build_dir
	
	cmake --config ../.. -GNinja
	
	# we store the timestamp of the last built app executable
	local last_modified_time=0
	local last_app_time=0
	last_app_time=`tar cf - modules ${app_name} local_resources resources | md5sum`
	
	# if no previous executable exists, we must still set the 
	# last_modified_time to something sensible
	if test $? -eq 0 
	then
		last_modified_time=$last_app_time
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

		local current_modified_time=`tar cf - modules ${app_name} local_resources resources | md5sum`
		popd

		if [[ $current_modified_time != $last_modified_time ]];
		then
			# we have a new artifact
			echo "time mismatch: $current_modified_time != $last_modified_time"  
			return 0
		else
			echo "$current_modified_time == $last_modified_time"
			# artifact has not changed.
			echo "no change" 
			return 2
		fi


	fi
}

process_app(){
	IFS=: read -ra app_names -d '' <<<"$1"
	local app_dir=${app_names[0]}
	local app_name=${app_names[1]}
	local app_base_dir="../apps/dev/$app_dir"

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
			return 1
		fi

		echo -n "[  OK  ] Build : ${app_name}"
		
		run_app $build_dir $app_name &>run.log

		if test $? -ne 0 
		then
			echo -n "[ FAIL ] Run   : ${app_name}"
			echo "--------------" >> run.err
			echo "${app_name} RUN FAILED: " >> run.err
			echo "--------------" >> run.err
			cat run.log >> run.err
			return 1
		fi

		echo -n "[  OK  ] Run   : ${app_name}"

		copy_screenshot $build_dir $app_name

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

for a in "${arr[@]}"; do 
	b=`echo $a | tr -d [:space:]`; 
	process_app $b
done
