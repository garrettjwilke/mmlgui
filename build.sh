#!/usr/bin/env bash

git checkout rng-patterns
git submodule update --init --recursive
cd ctrmml
git checkout rng-patterns
cd ..

if [ -d build ]
then
  rm -rf build
fi

mkdir build
cd build

if [ $(uname) == "Darwin" ]
then
  cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ..
else
  cmake ..
fi

cmake --build .
