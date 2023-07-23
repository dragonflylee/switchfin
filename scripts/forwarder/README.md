# 构建 NSP forwarder （桌面图标）

bash
```shell
make -C scripts/forwarder Switchfin.nacp

hacbrewpack -k prod.keys --titleid 010FF000FFFF0003 --titlename Switchfin --noromfs --nologo
```

# Thanks to

https://github.com/The-4n/hacBrewPack
https://github.com/switchbrew/nx-hbloader
