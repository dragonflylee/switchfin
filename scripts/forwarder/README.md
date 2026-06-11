# Forwarder NSP (tuile d'accueil Switch)

Le forwarder est un NSP installable qui lance `sdmc:/switch/pleNx.nro` comme
un jeu (session application complète). Il est construit automatiquement avec
`-DBUILTIN_NSP=ON` et embarqué dans le NRO (installation proposée au premier
lancement en mode applet).

Construction manuelle :

```shell
make -C scripts/forwarder pleNx.nacp

hacbrewpack -k prod.keys --titleid 0104201312000000 --titlename pleNx --noromfs --nologo
```

Le keyset : fournir le vôtre via `-DPROJECT_KEYSET=/chemin/prod.keys` (dumpé avec
Lockpick_RCM), sinon le CMake en télécharge un public (comportement CI).

# Thanks to

https://github.com/The-4n/hacBrewPack
https://github.com/switchbrew/nx-hbloader
