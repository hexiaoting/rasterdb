#!/bin/bash

set -x

# install packages needed to build and run GPDB
curl -L https://pek3a.qingstor.com/hashdata-public/tools/centos7/libarchive/libarchive.repo -o /etc/yum.repos.d/libarchive.repo

sudo yum -y groupinstall "Development tools"

sudo yum -y install epel-release

sudo yum -y install \
	ed \
	vim \
	which \
	wget \
	git \
	curl-devel \
	readline-devel \
	libyaml-devel \
	libxml2-devel \
	openssl-devel \
	cmake \
	make \
	gcc-c++ \
	boost-devel \	
