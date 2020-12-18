#!/bin/bash
function run()
{
cd /root/DeploymentScripts/
touch LastBuiltDevelopment
oldHash=$(tac LastBuiltDevelopment | head -n 1)
remoteDevelopment="https://github.com/galpHub/Divi.git"
git clone --depth=1 $remoteDevelopment --branch Development /tmp/Divi/
pushd /tmp/Divi/
newestHash=$(git rev-parse HEAD)
popd
rm -r /tmp/Divi/
zero=0
if [[ $oldHash != $newestHash ]]; then
	./tidy_build.sh Development $remoteDevelopment $1
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
run $1
