# KC705用PCIe HIDキーボードドライバ (仮)

## ビルド方法
```
make clean
make
make install
sudo rmmod pciehid
sudo modprobe pciehid
```