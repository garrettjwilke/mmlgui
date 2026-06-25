#!/usr/bin/env bash

git submodule update --init --recursive

pushd MDSDRV/sjasmplus
git checkout master
popd

if [ -d build ]
then
  rm -rf build
fi

if [ -f mmlgui-ssd ]
then
  rm mmlgui-ssd
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

if [ -f mmlgui-ssd ]
then
  mv mmlgui-ssd ../
  echo "build success! run ./mmlgui-ssd"
else
  echo "build failed"
fi
