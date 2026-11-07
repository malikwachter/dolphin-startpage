#!/bin/sh
# Build & install for Plasma 6 (KF6). Use -DBUILD_WITH_QT6=OFF for Plasma 5.
set -e
cd "$(dirname "$0")"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr "$@"
cmake --build build -j"$(nproc)"
sudo cmake --install build
echo "Done. Restart Dolphin:  killall dolphin; dolphin startpage:/"
