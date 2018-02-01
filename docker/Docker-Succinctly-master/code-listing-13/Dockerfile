FROM ubuntu
MAINTAINER Elton Stoneman <elton@sixeyed.com>

RUN apt-get update && \
    apt-get install -y netcat-openbsd

ENV LOG_FILE echo.out

EXPOSE 8082

VOLUME /server-logs

COPY ./echoserver.sh /echoserver.sh

RUN chmod +x /echoserver.sh

CMD /echoserver.sh
