Release Process Procedure (For Ubuntu 20.04 OS base machine - e.g. Digital Ocean)
* sudo apt-get update
* sudo apt-get -y upgrade
* sudo apt-get install -y ruby rename
* git clone --depth=1 <development-repo> Divi
* cp Divi/DeploymentScripts DeploymentScripts && cd DeploymentScripts
* ./install_docker_20-04.sh
* sudo chmod +x download_dependencies.sh continuous_build.sh tidy_build.sh build.sh
* ./download_dependencies.sh
If development binaries are desired:
* rm LastBuiltDevelopment && nohup ./continuous_build.sh dev > build_process.log 2>&1 &
If production binaries are desired:
* rm LastBuiltDevelopment && nohup ./continuous_build.sh > build_process.log 2>&1 &

Additonal notes:
* Should have at least 4 ram and 2 cpus of hardware for building
* Built binaries are stored under /divi_binaries/
* LastBuiltDevelopment is a list of recently built commit hashes. The build process ignores the last commit hash in this file.
* It's important to run the build process in the background as it can slow down the reponsiveness of the shell on which it executes potentially cancelling the build in the event of disconnection