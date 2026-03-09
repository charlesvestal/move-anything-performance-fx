#!/bin/bash

set -e

rm -rf ./artifacts ./install
mkdir -p ./artifacts ./install

for PRESET in \
    apple-mac \
    apple-ios \
; do
    cmake \
        --preset ${PRESET} \
        -S . \
        -B builds/${PRESET} \
        -D BUNGEE_PRESET=${PRESET}  \
        -D CMAKE_INSTALL_PREFIX=./install \
        -D BUNGEE_VERSION=$1
    cmake \
        --build builds/${PRESET} \
        --config Release \
        -j 3
    cmake \
        --install builds/${PRESET}
done
