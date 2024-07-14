#!/bin/bash

set -e

JOBS="$(nproc || echo 1)"

rm -rf build
mkdir build
pushd build
  #git clone https://github.com/tdlib/td.git
  tar zxf ../td.tar.gz
  pushd td
    #git checkout 1d1bc07eded7d3ee7df7c567e008bbf926cba081
    #git checkout 63c7d0301825b78c30dc7307f1f1466be049eb79
    #git checkout 15db91b536d796778b628fef3d60923cef351512
    # 1.8.16
    #git checkout cde095db6c75827fe4bd237039574aad373ad96b
    # 1.8.15
    #git checkout 2e5319ff360cd2d6dab638a7e0370fe959e4201b
    #git checkout 14c570f3348a028949518b951704651bf1ded0ac
    #git checkout 5ef84c5c6510e21e362e46822b351be7366c3a1a
    git checkout 83dfdcd9ee9945ae1cde9b59097ad260e90dd3a9
    # 1.8.14
    #git checkout 8517026415e75a8eec567774072cbbbbb52376c1
    # 1.8.12
    #git checkout 0c09070ce5d36bcccc5533c89dea45909483f45a
    # 1.8.10
    #git checkout 93c42f6d7c1209937431469f80427d48907f1b8d
    # 1.8.8
    #git checkout bbe37ee594d97f3c7820dd23ebcd9c9b8dac51a0
    # 1.8.6
    #git checkout 15db91b536d796778b628fef3d60923cef351512
    # 1.8.4
    #git checkout 7eabd8ca60de025e45e99d4e5edd39f4ebd9467e
    #git checkout cb98c0a10afb245da06dbefcefdf44d6aea1fa3e
    #git checkout 014b45842578d01b5b7edc7a40f97f1666fdbe44
    #git checkout f9f309d334712794320c5d40957442a0b8202cdb
    # 1.8.3
    #git checkout 054a823c1a812ee3e038f702c6d8ba3e6974be9c
    # 1.8.2
    #git checkout 3f54c301ead1bbe6529df4ecfb63c7f645dd181c
    #1.8.1
    #git checkout 92c2a9c4e521df720abeaa9872e1c2b797d5c93f
    mkdir build
    pushd build
      cmake -DCMAKE_BUILD_TYPE=Release ..
      make -j "${JOBS}"
      make install DESTDIR=destdir
    popd
  popd
  cmake -DTd_DIR="$(realpath .)"/td/build/destdir/usr/local/lib/cmake/Td/ -DNoVoip=True ..
  make -j "${JOBS}"
  echo "Now calling sudo make install"
#  sudo make install
popd
