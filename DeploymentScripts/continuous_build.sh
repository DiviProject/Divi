#!/bin/bash
function run()
{
cd /root/DeploymentScripts/
touch LastBuiltDevelopment
oldHash=$(tac LastBuiltDevelopment | head -n 1)
remoteDevelopment="https://github.com/galpHub/Divi.git"
remoteMaster="https://github.com/DiviProject/Divi.git"

clean_mode=false
if [[ -z $1 ]] ; then
   branch_name="master"
   remoteRepo=$remoteMaster
   echo "Building master..."
elif [[ ! -z $1 ]] && [[ $1 == "dev" ]] ; then
   branch_name="Development"
   remoteRepo=$remoteDevelopment   
   echo "Building Development..."
fi

if [[ ! -z $2   ]] ; then
    clean_mode=true
fi

git clone --depth=1 $remoteRepo --branch $branch_name /tmp/Divi/
pushd /tmp/Divi/
newestHash=$(git rev-parse HEAD)
popd
rm -r /tmp/Divi/
zero=0
if [[ $oldHash != $newestHash ]]; then
	if $clean_mode ; then
		./tidy_build.sh $branch_name $remoteRepo clean
	else
		./tidy_build.sh $branch_name $remoteRepo
	fi
	if [[ $? ]]; then
		if [[ -d "/divi_binaries/$newestHash" ]]; then
			echo "Build complete!"
			echo "$newestHash" >>  LastBuiltDevelopment
			cd /divi_binaries/${newestHash}/
			tag=`basename "$PWD" | cut -c1-7`
			find . -type f -regex ".+\/divi-.*tar\.gz$" -print0 | xargs -0 rename "s/.tar.gz$/-${tag}.tar.gz/"
			find . -type f -regex ".+\/divi-.*\.zip$" -print0 | xargs -0 rename "s/\.zip$/-${tag}.zip/"
		else
			echo "Error: target hash $newestHash not built"
		fi
	else
		echo "Some builds may have failed!"
	fi
else
	echo "Up to date with Development binaries"
fi

}
run $1 $2
