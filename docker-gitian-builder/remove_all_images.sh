#!/bin/bash

docker images -q | awk '{system("docker rmi " $1)}'

