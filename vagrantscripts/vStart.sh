#! /bin/bash

echo "Copying scripts -------"
echo "Current location"
pwd
cd /var/scripts
sudo cp -v * /var/divi/PIVX-3.0/
cd /var/divi/PIVX-3.0/

echo "Starting update script: setupUb16.sh..."
./setupUb16.sh

