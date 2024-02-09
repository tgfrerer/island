#!/bin/bash

pushd resources || exit
curl -L "https://www.dropbox.com/s/vtcg03grmvryse8/images.tar.gz?dl=1" -o images.tar.gz
tar xfv images.tar.gz
popd || exit
