FROM ubuntu
MAINTAINER Elton Stoneman <elton@sixeyed.com>

RUN touch /setup.txt
RUN echo init > /setup.txt

COPY file.txt /b.txt
