#!/bin/bash

pushd resources || exit
curl -L "https://www.dropbox.com/scl/fi/9wdr9o6527bte0n24uwgv/images.tar.gz?rlkey=ywzszyy06f1woefm606uixq5r&dl=1" -o images.tar.gz
tar xfv images.tar.gz
popd || exit
