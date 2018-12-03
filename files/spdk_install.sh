#!/bin//bash

if [ ! -e spdk/.git ] ; then
 git clone https://github.com/spdk/dpdk.git
fi

cd dpdk

make defconfig
make
sudo make install prefix=/usr

cd ..

if [ ! -e spdk/.git ] ; then
 git clone https://github.com/spdk/spdk.git
fi

cd spdk

./configure --enable-debug --with-rdma --with-shared \
	--with-dpdk=/usr --disable-tests --prefix=/usr
make
sudo make install

sudo ldconfig
