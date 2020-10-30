apt-get update && 
apt-get -y install sudo &&
apt-get install -y git nano curl &&
sudo apt-get install -y git apache2 apt-cacher-ng python-vm-builder ruby qemu-utils &&
sudo apt install -y apt-transport-https ca-certificates curl software-properties-common &&
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add - &&
sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu bionic stable" &&
apt-cache search docker-ce &&
sudo apt install -y docker-ce
