function run()
{
rm -rf /blddv/ &&
mkdir -p /blddv/
mkdir -p /blddv/build_dependencies &&
cd /blddv/build_dependencies &&
curl -L https://github.com/phracker/MacOSX-SDKs/releases/download/10.13/MacOSX10.9.sdk.tar.xz | tar -xJf - || true &&
pushd MacOSX10.9.sdk &&
find -type f ! -iname "sha256checksums" -exec sha256sum "{}" + > sha256checksums &&
echo "8f68957bdf5f62b25e4187c3dc3ac994b24230f7d130165754235ea9e405ceb1  sha256checksums" | sha256sum -c  &&
rm sha256checksums && popd &&
tar -cJf MacOSX10.9.sdk.tar.xz ./ &&
rm -r MacOSX10.9.sdk &&
git clone -q https://github.com/raspberrypi/tools.git &&
tar -czf raspberrypi-tools.tar.gz ./tools &&
rm -rf ./tools &&
git clone -q https://github.com/DiviProject/gitian-builder.git /blddv/gitian-builder/ &&
sed -i -e "s/RUN echo 'Acquire::http { Proxy /# RUN echo 'Acquire::http { Proxy /g" /blddv/gitian-builder/bin/make-base-vm &&
cd /blddv/gitian-builder/ && git add . &&
git config --global user.email "DevelopmentDivi" &&
git config --global user.name "DevBot" &&
git commit -m "Remove proxy"
}

run