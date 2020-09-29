#!/bin/bash

# list apps you want to test

apps_list=("
    examples/asterisks:Island-Asterisks
    examples/lut_grading_example:Island-LutGradingExample
    examples/hello_world:Island-HelloWorld
    examples/hello_triangle:Island-HelloTriangle
    examples/imgui_example:Island-ImguiExample
    examples/compute_example:Island-ComputeExample
    examples/multi_window_example:Island-MultiWindowExample
    dev/test_blob_polygon:Island-TestBlobPolygon
    dev/test_cubemap:Island-TestCubemap
    dev/test_rtx:Island-TestRtx
    dev/test_cgltf:Island-TestCgltf
    dev/test_path_appear:Island-TestPathAppear
    dev/test_2d_watchface:Island-Test2DWatchface
    dev/test_2d_thick_lines:Island-Test2DThickLines
    dev/test_2d:Island-Test2D
    dev/test_mt_rendering:Island-TestMtRendering
	dev/test_jobs:Island-TestJobs
	dev/blob_wave:Island-BlobWave
	dev/aeon:Island-Aeon
	dev/bb_spectrum:Island-BbSpectrum
	dev/fourier_blob:Island-FourierBlob
	dev/langtons_loops:Island-LangtonsLoops
	dev/physarum:Island-Physarum
	dev/quad_bezier:Island-QuadBezier
	dev/show_font:Island-ShowFont
	dev/test_blob:Island-TestBlob
	dev/test_blob_refraction:Island-TestBlobRefraction
	dev/test_bloom:Island-TestBloom
	dev/test_compute:Island-TestCompute
	dev/test_dependency_tracker:Test_Dependency_Tracker
	dev/test_depth_draw:Island-TestDepthDraw
	dev/test_ecs:Island-TestEcs
	dev/test_font:Island-TestFont
	dev/test_font_path:Island-TestFontPath
	dev/test_font_refraction:Island-TestFontRefraction
	dev/test_glsl_include:Island-TestGlslInclude
	dev/test_img_attachment:Island-TestImgAttachment
	dev/test_img_swapchain:Island-TestImgSwapchain
	dev/test_mesh_generator:Island-TestMeshGenerator
	dev/test_mipmaps:Island-TestMipMaps
	dev/test_multisample:Island-TestMultisample
	dev/test_outline:Island-TestOutline
	dev/test_paintbrush:Island-TestPaintbrush
	dev/test_param:Island-TestParam
	dev/test_path:Island-TestPath
	dev/test_path_render:Island-TestPathRender
	dev/test_ping_pong:Island-TestPingPong
	dev/test_ply:Island-TestPly
	dev/test_poisson_blur:Island-TestPoissonBlur
	dev/test_resolve:Island-TestResolve
	dev/test_sdf:Island-TestSdf
	dev/test_setup_pass:Island-TestSetupPass
	dev/test_trails:Island-TestTrails
	dev/test_turntable:Island-TestTurntable
	dev/test_verlet:Island-TestVerlet
	dev/test_blob_trails:Island-TestBlobTrails
")

TAKE_SCREENSHOTS=0

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
	ln -f ${screenshot_dir}/${app_name}.png ${build_dir}/../../screenshot.png
}

# run app and takes screenshot using the vulkan screenshot layer
run_app(){
	local build_dir=$1
	local app_name=$2

	pushd $build_dir
	env VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES="60" ./${app_name} &
	local app_pid=$! 
	
	sleep 7
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
	IFS=: read -ra app_names -d '' <<<"$1"
	local app_dir=${app_names[0]}
	local app_name=${app_names[1]}
	local app_base_dir="../apps/$app_dir"

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

		# we return early if screenshots have not been requested explicitly
		if [[ $TAKE_SCREENSHOTS != 1 ]];
		then
			return 0
		fi
		
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

# check if we want to make screenshots as well
# as compiling apps.

while getopts ":s" opt; do
  case ${opt} in
    s ) # take screenshots
	  TAKE_SCREENSHOTS=1
	  echo "requesting screenshots."
      ;;
    \? ) echo "Usage: cmd [-s]"
      ;;
  esac
done


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
