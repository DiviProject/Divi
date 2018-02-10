#!/bin/bash

docker ps -a | awk '{system("docker rm " $1)}'

