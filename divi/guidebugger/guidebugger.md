docker pull dcaldwellsw/divi:guidebugger
docker run -p 51472:51472 -p 22:22 --detach -it dcaldwellsw/divi:guidebugger
docker ps
# you need the currently running docker container ID for the next command
docker exec -it [containerid] /bin/bash 

# now you can run commands like pulling the git and rebuilding the project.
# open ModXterm and press the button: "Start Local Terminal" 

ssh -X -v ubuntu@127.0.0.1

# password is 'ubuntu' as set in the Dockerfile.
# now open Visual Studio Code:
code

# a window will pop open after a few moments.  
# open the repo's 'divi' folder in VSCode app
# after opening vscode you will have a .vscode folder in that folder you opened

# There are 2 config files for vscode in the home (~) folder: c_cpp_properties.json & launch.json
# Copy these files into the desired .vscode folder, close vscode and reopen it.
# There should now be 2 configurations in the debug menu: Launch and Attach.