#!/bin/bash

set -e

JOBS="$(nproc || echo 1)"

rm -rf build
mkdir build
pushd build
  #git clone https://github.com/tdlib/td.git
  tar zxf ../td.tar.gz
  pushd td
    git checkout 2589c3fd46925f5d57e4ec79233cd1bd0f5d0c09
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
  sudo make install
popd
