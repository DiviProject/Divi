#!/bin/bash

function run()
{

export USE_DOCKER=1
export COMMIT=master
MayBuild=false
if [[ -n $1 ]]; then
	export COMMIT=$1
fi

rm -rf /blddv/Divi/ &&
mkdir -p /blddv/Divi/

HEAD_COMMIT_HASH=""
source_url="https://github.com/DiviProject/Divi.git"
if [[ -n $2 ]]; then
	source_url=$2
fi

git clone -q $source_url /blddv/Divi/ &&
cd /blddv/Divi/ &&
git checkout -q $1 || git checkout -q origin/$1 -b $1 &&
HEAD_COMMIT_HASH=$(git rev-parse HEAD) &&
MayBuild=true

last_failed_build=""
if ! $MayBuild; then
	echo "Cannot build current configuration"
fi
all_builds_succeeded=true
builds=(linux win osx rpi2)
if $MayBuild; then
	echo "Working from commit: $1 -> $COMMIT & source: $2"
	rm -rf /blddv/upload/ &&
	mkdir -p /blddv/upload/

	cd /blddv/gitian-builder/ &&
	mkdir -p /blddv/gitian-builder/inputs/ &&
	cp /blddv/build_dependencies/* /blddv/gitian-builder/inputs/

	[ -d "/blddv/gitian-builder/inputs/divi/" ] && rm -rf /blddv/gitian-builder/inputs/divi/

	cd /blddv/gitian-builder/ &&
	for build_id in ${builds[@]}
	do
		if [ "${last_failed_build}" = "${build_id}" ]; then all_builds_succeeded=false && break ; fi
		last_failed_build="${build_id}" &&
		/blddv/gitian-builder/bin/make-base-vm --docker --suite bionic && wait &&
		/blddv/gitian-builder/bin/gbuild -j 1 --commit divi=$COMMIT --url divi=/blddv/Divi/ /blddv/Divi/divi/contrib/gitian-descriptors/gitian-${build_id}.yml &&
		mkdir -p /blddv/upload/${build_id} &&
		cp -r /blddv/gitian-builder/build/out/* /blddv/upload/${build_id}/ &&
		last_failed_build="" &&
		 docker container stop $( docker ps -a -q )
	done
fi

mkdir -p /divi_binaries/
binaries_folder="/divi_binaries/$HEAD_COMMIT_HASH"
mkdir -p $binaries_folder
if $all_builds_succeeded ; then
	cp -r /blddv/upload/* $binaries_folder
	echo "Build succeeded"
	return 0
else
	cp /blddv/gitian-builder/var/*.log $binaries_folder &&
	cd $binaries_folder &&
	rename "s/.log/_$last_failed_build$HEAD_COMMIT_HASH.log/" *
	echo "Build failed for $HEAD_COMMIT_HASH from branch $COMMIT"
	return 1
fi
}

run $1 $2
