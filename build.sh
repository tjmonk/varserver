#!/bin/sh

# build the client shared object first
mkdir -p client/build
cd client/build
cmake ..
make clean
make
sudo make install
sudo ldconfig

# now build everything else
cd ../..
mkdir -p build && cd build
cmake ..
make clean
make
sudo make install
cd ..


