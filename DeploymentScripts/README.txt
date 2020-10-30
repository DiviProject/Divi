# This tool was designed to be used in an Ubuntu 18.04 environment
# It provides a quick way of building divi binaries without all the
# maneuvering that's otherwise required. A few dependencies like the MacOSX10.9 sdk
# as well as the raspberrypi-tools. These are downloaded as part of the tidy_build.sh
# but require care as these tools have not been thoroughly tested against careless
# usage.
# First time usage should be './continuous_build.sh clean'
# Later usages that wish to skip the download of the 3rd party dependencies
# can ommit the call to clean
# Note that these deployment scripts point to a specific remote repo and branch
# namely the development branch being managed by @galpHub. These should be updated
# whenever these change

## Pre-requisites to be run in ~/build_dependencies
# curl -L https://github.com/phracker/MacOSX-SDKs/releases/download/10.13/MacOSX10.9.sdk.tar.xz | tar -xJf - &&
# tar -cJf MacOSX10.9.sdk.tar.xz ./ &&
# rm -r MacOSX10.9.sdk &&
# git clone -q https://github.com/raspberrypi/tools.git && tar -czf raspberrypi-tools.tar.gz ./tools &&
# rm -rf ./tools
