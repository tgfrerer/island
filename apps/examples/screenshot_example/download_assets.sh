#!/bin/bash

pushd resources || exit

if [ ! -e resources.tar.gz ]; then
    curl -L "https://www.dropbox.com/s/vtcg03grmvryse8/images.tar.gz?dl=1" -o resources.tar.gz
fi

tar xfv resources.tar.gz images/world_winter.jpg

popd || exit
