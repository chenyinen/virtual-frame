# virtual-ipc
在缺少码流数据源的情况下可以用此模拟码流数据
# 依赖
工程依赖ffmpeg中的libavcodec libavformat libavutil libswscale库,如果环境缺少库先安装.
```
apt install libavcodec-dev
apt install libavformat-dev
apt install libavutil-dev
apt install libswscale-dev
```
# 构建
```
cmake -S . -B build
cd build
make
./example
```
