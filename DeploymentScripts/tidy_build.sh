#!/bin/bash
function run()
{
readyToBuild=false

case "$3" in
clean)
   rm -rf /blddv/ &&
   mkdir -p /blddv/
   mkdir -p /blddv/build_dependencies &&
   cd /blddv/build_dependencies &&
   curl -L https://github.com/phracker/MacOSX-SDKs/releases/download/10.13/MacOSX10.9.sdk.tar.xz | tar -xJf - || true &&
   tar -cJf MacOSX10.9.sdk.tar.xz ./ &&
   rm -r MacOSX10.9.sdk &&
   git clone -q https://github.com/raspberrypi/tools.git &&
   tar -czf raspberrypi-tools.tar.gz ./tools &&
   rm -rf ./tools &&
   git clone -q https://github.com/devrandom/gitian-builder.git /blddv/gitian-builder/ &&
   sed -i -e "s/RUN echo 'Acquire::http { Proxy /# RUN echo 'Acquire::http { Proxy /g" /blddv/gitian-builder/bin/make-base-vm &&
   cd /blddv/gitian-builder/ && git add . &&
   git config --global user.email "DevelopmentDivi" &&
   git config --global user.name "DevBot" &&
   git commit -m "Remove proxy" &&
   readyToBuild=true

;;
*)
   readyToBuild=true
esac

if $readyToBuild ; then
	cd /root/DeploymentScripts/ &&
	bash ./build.sh $1 $2
	return $?
else
	return 0
fi
}
run $1 $2 $3
