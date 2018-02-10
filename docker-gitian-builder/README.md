# Universal Docker Gitian Builder 

1.	Install Docker as described at https://docs.docker.com/get-started/
2.	Open shell, command prompt or powershell
3.	Create a directory (e.g. mkdir pivx)
4.	Move into new directory (e.g. cd pivx)
5.	Copy these files into directory
6.	Execute the build_builder.sh shell script
7.	Wait until done (5 – 10 minutes)
8.	Execute the run_builder.sh shell script
9.	Wait until done (generally about an hour for most cryptocurrency builds)

The Dockerfile is easily modified for any coin from any repo (with a gitian-build YAML file) . . . .

Simply modify the bottom six lines as follows
                In the four run lines, change the four instances of pivx to your coin name
                In the CMD line, replace the arguments with the commit tag, the repo URL and the path to the gitian-build YAML file

EXAMPLES
                Bitcoin
		RUN printf "[[ -d /shared/bitcoin ]] || \
		git clone -b \$1 --depth 1 \$2 /shared/bitcoin && \
		cd /shared/gitian-builder; \
		./bin/gbuild --skip-image --commit bitcoin=\$1 --url bitcoin=\$2 \$3" > /home/ubuntu/runit.sh
		CMD ["v1.14.1rc2","https://github.com/btc1/bitcoin.git","../bitcoin/contrib/gitian-descriptors/gitian-linux.yml"]
		ENTRYPOINT ["bash", "/home/ubuntu/runit.sh"]
                PIVX
		RUN printf "[[ -d /shared/pivx ]] || \
		git clone -b \$1 --depth 1 \$2 /shared/pivx && \
		cd /shared/gitian-builder; \
		./bin/gbuild --skip-image --commit pivx=\$1 --url pivx=\$2 \$3" > /home/ubuntu/runit.sh
		CMD ["v3.0.6","https://github.com/PIVX-Project/pivx.git","../pivx/contrib/gitian-descriptors/gitian-linux.yml"]
		ENTRYPOINT ["bash", "/home/ubuntu/runit.sh"]



