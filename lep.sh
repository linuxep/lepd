#!/bin/bash
set -e

echo "Install Docker"
if [ "$(which docker)" == "" ]; then
curl -sSL http://acs-public-mirror.oss-cn-hangzhou.aliyuncs.com/docker-engine/internet | sh -
fi
sudo usermod -aG docker jiangenj
echo "Please logout and login to run the script again"

sudo mkdir -p /etc/docker
sudo tee /etc/docker/daemon.json <<-'EOF'
{
  "registry-mirrors": ["https://ahbake4w.mirror.aliyuncs.com"]
}
EOF
sudo systemctl daemon-reload
sudo systemctl restart docker

echo "Test docker install"
docker run hello-world

echo "Prepare OS environment"
sudo apt-get install libev-dev linux-tools-common linux-tools-`uname -r` libncurses5-dev gcc-arm-linux-gnueabi


echo "Sync lepd"
git clone https://github.com/linuxep/lepd
echo "Sync lepv"
git clone https://github.com/linuxep/lepv

echo "Build lepd"
cd lepd
make
make install
make clean
make ARCH=arm
make ARCH=arm install

echo "Run lepd with below command on x86"
echo "sudo ./prebuilt-binaries/x86-lepd"

echo "Run lepd with below command on arm"
echo "sudo ./prebuilt-binaries/arm-lepd"
