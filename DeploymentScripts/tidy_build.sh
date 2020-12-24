#!/bin/bash
function run()
{
readyToBuild=false

case "$3" in
clean)
   pushd ~/DeploymentScripts/
   ./download_depencies && 
   popd &&
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
