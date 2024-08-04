#!/bin/bash

set -e

JOBS="$(nproc || echo 1)"

rm -rf build
mkdir build
pushd build
  #git clone https://github.com/tdlib/td.git
  tar zxf ../td.tar.gz
  pushd td
    git checkout d93a99e3351db82573d765ce4f5e84714c277518
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
