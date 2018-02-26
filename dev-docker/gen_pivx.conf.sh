#! /bin/bash
# ex: ./gen.pivx.conf.sh rpcdivi1 F6NBUqxucW5ayKHgTGRYxWTybUQd58EC8N7N3zAWh6L6 127.0.0.1

echo "rpcuser=$1
rpcpassword=$2
rpcallowip=$3
rpcport=51473
daemon=1" >> ~/.pivx/pivx.conf
