#!/bin/bash

set -e

rm -rf ./artifacts ./install
mkdir -p ./artifacts ./install

for PRESET in \
    linux-aarch64 \
    linux-x86_64 \
    windows-x86_64 \
    android-armeabi-v7a \
    android-arm64-v8a \
    android-x86 \
    android-x86_64 \
; do
    cmake \
        --preset ${PRESET} \
        -S . \
        -B ./builds/${PRESET} \
        -D BUNGEE_PRESET=${PRESET}  \
        -D CMAKE_INSTALL_PREFIX=./install \
        -D BUNGEE_VERSION=$1  \
        -G "Unix Makefiles"
    cmake \
        --build ./builds/${PRESET} \
        --target install
done
