#!/usr/bin/env bash

git checkout rng-patterns
git submodule update --init --recursive
cd ctrmml && git checkout rng-patterns
