# KC705をキーボードとして利用するためのドライバ

## ビルド方法
```
make clean
make
make install
sudo rmmod pciehid
sudo modprobe pciehid
```