# CLAUDE.md — aic8800-wifi-bt-driver

## Cos'è

Driver Linux (moduli del kernel in C) per la chiavetta USB AIC8800D80 («88M80», Wi-Fi 6 +
Bluetooth 5.4, clone «Pandora»), più firmware, mode-switch e installer. Obiettivo: usare tutto
l'hardware della chiavetta su qualunque computer Linux con kernel recente. Sorgenti derivate da
radxa-pkg/aic8800 e olamellberg/AIC8800D80, con patch di compatibilità per i kernel 7.x.
Repo privato `GabryXnLab/aic8800-wifi-bt-driver`; push su `main` autorizzato, mai force push.
Banco di prova: il PC `ohos` (NixOS, kernel CachyOS Clang/ThinLTO), che lo usa come input flake.

## Stack

C (Kbuild, moduli out-of-tree), DKMS, shell, systemd/udev/usb_modeswitch. Gli strumenti
userspace nuovi saranno in Rust; i moduli restano in C (un modulo Rust richiede `CONFIG_RUST`,
assente quasi ovunque).

## Struttura

```
dkms-src/             # sorgenti dei tre moduli, un unico pacchetto DKMS (dkms.conf)
  aic_load_fw/        # carica il firmware dal USB a69c:8d80, esporta simboli e parametro aic_fw_path
  aic8800_fdrv/       # driver Wi-Fi (cfg80211/FullMAC, prefisso rwnx_), dipende dai simboli di aic_load_fw
  aic_btusb/          # driver Bluetooth (sostituisce btusb per questa chiavetta)
firmware/             # blob .bin di aic8800D80, copiati in <fw>/aic8800D80/
usb_modeswitch/ systemd/ udev/ modprobe/ modules-load.d/   # da 1111:1111 a a69c:8d80 -> 8d81
install.sh uninstall.sh
```

## Comandi

```bash
sudo bash install.sh        # installa via DKMS (firmware, regole udev, modprobe)
sudo bash uninstall.sh
dkms status | grep aic8800
```

## Note architetturali importanti

- La chiavetta nasce come CD-ROM `1111:1111`; `usb_modeswitch` la porta a `a69c:8d80`,
  `aic_load_fw` carica il firmware e il dispositivo si ripresenta come `a69c:8d81` (Wi-Fi + BT).
  La regola udev avvia un servizio systemd, non `RUN`: su questa revisione `RUN` arriva troppo presto.
- `btusb` generico non sa inizializzare il controller (timeout su HCI_Reset): `softdep btusb pre:
  aic_btusb` in `modprobe/aic8800-bt.conf` è indispensabile.
- Il firmware si legge con `filp_open` da un percorso (non `request_firmware`): `aic_load_fw`
  lo calcola e `aic8800_fdrv` lo riceve da `get_fw_path()`, poi aggiunge il sottodirectory del chip.
- `aic8800_fdrv` usa simboli esportati da `aic_load_fw`: compilare prima `aic_load_fw` e passare
  `KBUILD_EXTRA_SYMBOLS=…/aic_load_fw/Module.symvers`. `aic_btusb` si compila con
  `CONFIG_PLATFORM_UBUNTU=y` (seleziona il percorso del firmware con sottodirectory).
- Dopo aver cambiato i moduli su un sistema con `boot.extraModulePackages` serve un riavvio
  (`modprobe` legge `/run/booted-system`); per provare una modifica senza riavviare, `rmmod`/`insmod`.

## Convenzioni

Italiano per commit e documentazione; commit atomici; niente refactor non richiesti; commenti
solo per un «perché» non ovvio. Commit che terminano con la riga `Co-Authored-By`; messaggi con
backtick in un file e `git commit -F`.
