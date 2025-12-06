#!/usr/bin/env bash

#git checkout rng-patterns
git submodule update --init --recursive

if [ -d build ]
then
  rm -rf build
fi

if [ -f mmlgui-rng ]
then
  rm mmlgui-rng
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

if [ -f mmlgui-rng ]
then
  mv mmlgui-rng ../
  echo "build success! run ./mmlgui-rng"
else
  echo "build failed"
fi
