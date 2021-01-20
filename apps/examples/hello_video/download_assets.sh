#!/bin/bash

pushd resources
curl -L https://test-videos.co.uk/vids/jellyfish/mp4/h264/1080/Jellyfish_1080_10s_20MB.mp4 -o test.mp4
popd
