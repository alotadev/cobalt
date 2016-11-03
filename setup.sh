#!/bin/sh
#
# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Installs cobalt dependencies.
# Warning this will install packages globally

PREFIX=/usr

if [ $(id -u) != "0" ] ; then
    echo run as root
    exit 1
fi

echo "This will install cobalt dependencies globally ($PREFIX)"
echo "Enter y to proceed"

read Y

if [ "$Y" != 'y' ] ; then
    echo Aborting...
    exit 1
fi

# Current dir
CD=$(pwd)

# scratch dir (if needed)
WD=/tmp/cobalt_setup
rm -fr $WD
mkdir $WD

# Install apt packages
apt-get -y install clang cmake ninja-build libgflags-dev libgoogle-glog-dev

# Install golang 1.7.1
GO_DIR=/usr/local/go

if ls $GO_DIR > /dev/null ; then
    GO_VERSION=$($GO_DIR/bin/go version | awk '{print $3}')
fi

if [ "$GO_VERSION" != "go1.7.1" ] ; then
    if [ -d $GO_DIR ] ; then
        echo "Removing the incompatible golang package...: ${GO_VERSION}"
        rm -rf $GO_DIR
    fi
    cd $WD
    wget https://storage.googleapis.com/golang/go1.7.1.linux-amd64.tar.gz
    tar -C /usr/local -xvf go1.7.1.linux-amd64.tar.gz
    export PATH=/usr/local/go/bin:$PATH
fi

# Install protobuf 3
if ! which protoc > /dev/null ||
   [ "$(protoc --version | awk '{print $2}')" != "3.0.0" ] ; then
    cd $WD
    wget https://github.com/google/protobuf/releases/download/v3.0.2/protobuf-cpp-3.0.2.tar.gz
    tar zxf protobuf-cpp-3.0.2.tar.gz
    cd protobuf-3.0.2
    ./configure --prefix=$PREFIX
    make install
fi

# Install gRPC
if ! which grpc_cpp_plugin > /dev/null ; then
    cd $WD
    git clone -b v1.0.0 https://github.com/grpc/grpc
    cd grpc
    git submodule update --init
    make prefix=$PREFIX install
fi

# Install google APIs (C++ protobuf stubs)
if [ ! -f /usr/lib/libgoogleapis.so ] ; then
    cd $WD
    git clone https://github.com/googleapis/googleapis.git
    cd googleapis
    git checkout 14263af2cbe711f2f2aa692da1b09a8311a2e45d
    make LANGUAGE=cpp GPRCPLUGIN=`which grpc_cpp_plugin`

    echo
    echo Compiling libgoogleapis.  This will take 5 minutes.
    cd gens
    find . -name \*.cc \
      | xargs clang++ -o libgoogleapis.so -fPIC -shared --std=c++11 -I .

    mv libgoogleapis.so /usr/lib
    ldconfig

    # copy headers
    cd google
    find . -name \*.h -exec cp --parents {} /usr/include/google/ \;
fi

# Build and install protoc-gen-go binary
GOPATH=${CD}/third_party/go
export GOPATH
if ! which protoc-gen-go > /dev/null ; then
    cd $WD
    go build -o $PREFIX/bin/protoc-gen-go github.com/golang/protobuf/protoc-gen-go
fi

unset GOPATH

rm -fr $WD
