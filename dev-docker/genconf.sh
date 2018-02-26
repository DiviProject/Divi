#! /bin/bash
# example execution:   folder username password
# 			./genconf.sh divi rpcdivi1 F6NBUqxucW5ayKHgTGRYxWTybUQd58EC8N7N3zAWh6L6 

echo "rpcuser=$2
rpcpassword=$3
rpcallowip=127.0.0.1
rpcport=51473
daemon=1" >> ~/.$1/$1.conf
