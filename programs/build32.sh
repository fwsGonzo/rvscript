#!/bin/bash
set -e
GCC_TRIPLE="riscv32-unknown-elf"
export CC=$GCC_TRIPLE-gcc
export CXX=$GCC_TRIPLE-g++

mkdir -p $GCC_TRIPLE
pushd $GCC_TRIPLE
cmake .. -DGCC_TRIPLE=$GCC_TRIPLE -DCMAKE_BUILD_TYPE=Release -DLTO=ON -DCMAKE_TOOLCHAIN_FILE=../micro/toolchain.cmake
make -j4
popd
