# CLAUDE.md — aic8800-wifi-bt-driver

## Cos'è

Driver Linux (moduli del kernel in C) per la chiavetta USB AIC8800D80 («88M80», Wi-Fi 6 +
Bluetooth 5.4, clone «Pandora»), più firmware, mode-switch e installer. Obiettivo: usare tutto
l'hardware della chiavetta su qualunque computer Linux con kernel recente. Sorgenti derivate da
radxa-pkg/aic8800 e olamellberg/AIC8800D80, con patch di compatibilità per i kernel 7.x.
Repo privato `GabryXnLab/aic8800-wifi-bt-driver`; push su `main` autorizzato, mai force push.
Banco di prova: il PC `ohos` (NixOS, kernel CachyOS Clang/ThinLTO), che lo usa come input flake.

## Stack

C (Kbuild, moduli out-of-tree), DKMS, Nix (flake con modulo NixOS), shell, systemd/udev/usb_modeswitch. Gli strumenti
userspace nuovi saranno in Rust; i moduli restano in C (un modulo Rust richiede `CONFIG_RUST`,
assente quasi ovunque).

## Struttura

```
dkms-src/             # sorgenti dei tre moduli, un unico pacchetto DKMS (dkms.conf); Makefile radice
                      # che li compila nell'ordine giusto
  aic_load_fw/        # carica il firmware dal USB a69c:8d80, esporta simboli e parametro aic_fw_path
  aic8800_fdrv/       # driver Wi-Fi (cfg80211/FullMAC, prefisso rwnx_), dipende dai simboli di aic_load_fw
  aic_btusb/          # driver Bluetooth (sostituisce btusb per questa chiavetta)
flake.nix nix/        # lib.mkDriver, nixosModules.default (hardware.aic8800.enable), packages.driver-<kernel>
firmware/             # blob .bin di aic8800D80, copiati in <fw>/aic8800D80/
usb_modeswitch/ systemd/ udev/ modprobe/ modules-load.d/   # da 1111:1111 a a69c:8d80 -> 8d81
install.sh uninstall.sh
```

## Comandi

```bash
sudo bash install.sh                        # distro generica: apt/dnf/pacman/zypper + DKMS
sudo bash uninstall.sh
make -C dkms-src [KVER=… KDIR=… AIC_FW_DIR=…]   # build senza DKMS; "sudo make -C dkms-src install"
nix build .#driver-6_12                     # matrice: 5_15 6_1 6_6 6_12 6_18 7_2 (nixpkgs 26.05)
```

### Ciclo rapido su ohos (senza rebuild di NixOS)

```bash
rsync -a --delete --exclude .git ./ ohos:aic8800-work/ && ssh ohos 'cd aic8800-work && git init -q 2>/dev/null; git add -A'
ssh ohos 'cd aic8800-work && nix build --no-link --print-out-paths --impure --expr "let c = (builtins.getFlake \"/etc/nixos\").nixosConfigurations.ohos; in (builtins.getFlake (toString ./.)).lib.mkDriver { pkgs = c.pkgs; kernelPackages = c.config.boot.kernelPackages; }"'
# poi, con D=<out>/lib/modules/7.2.9-cachyos/updates/aic8800:
sudo rmmod aic_btusb aic8800_fdrv aic_load_fw; sudo insmod $D/aic_load_fw.ko; sudo insmod $D/aic8800_fdrv.ko; sudo insmod $D/aic_btusb.ko
```

Subito dopo `rmmod` fare gli `insmod` (senza pause): la chiavetta si resetta da sola e, se non trova
i moduli caricati, udev ci carica quelli del sistema avviato. Attendere ~15 s (riappare `wlan0`).
Verifica che giri il modulo nuovo: `cat /sys/module/aic_load_fw/srcversion` uguale a
`modinfo -F srcversion $D/aic_load_fw.ko`. Wi-Fi e BT cadono per qualche secondo (ohos ha anche
`eno1`). Per renderlo definitivo: commit, push, `nix flake update aic8800` in nixos-setup e
`/etc/nixos`, rebuild e riavvio.

## Note architetturali importanti

- La chiavetta nasce come CD-ROM `1111:1111`; `usb_modeswitch` la porta a `a69c:8d80`,
  `aic_load_fw` carica il firmware e il dispositivo si ripresenta come `a69c:8d81` (Wi-Fi + BT).
  La regola udev avvia un servizio systemd, non `RUN`: su questa revisione `RUN` arriva troppo presto.
- `btusb` generico non sa inizializzare il controller (timeout su HCI_Reset): `softdep btusb pre:
  aic_btusb` in `modprobe/aic8800-bt.conf` è indispensabile.
- Il firmware si legge con `filp_open` da un percorso (non `request_firmware`): `aic_load_fw`
  lo calcola e `aic8800_fdrv` lo riceve da `get_fw_path()`, poi aggiunge il sottodirectory del chip.
  Il default è la macro `AIC_FW_DIR` (build, default `/lib/firmware`; su NixOS lo store). Il parametro
  `aic_fw_path` di `aic_load_fw` esiste ma NON aggiunge il sottodirectory `aic8800D80` come il default:
  non usarlo senza correggere i due comportamenti. File compressi (.xz) non sono letti.
- Le API di cfg80211 cambiano e vengono retroportate nei kernel stabili: dove il numero di versione
  non basta, `dkms-src/Makefile` guarda `include/net/cfg80211.h` e passa `-DAIC_*` (oggi
  `AIC_MONITOR_CHAN_HAS_DEV`, `AIC_GET_TX_POWER_HAS_RADIO`). Un nuovo errore di firma su un kernel
  stabile si risolve con una sonda nuova, non spostando soglie di versione.
- `aicbluetooth.c` ha righe CRLF: modificarlo in binario o con strumenti che le preservano.
- nixpkgs 26.05 non ha più i kernel 6.17, 6.19, 7.0, 7.1 (EOL): la matrice usa quelli presenti.
- `aic8800_fdrv` usa simboli esportati da `aic_load_fw`: compilare prima `aic_load_fw` e passare
  `KBUILD_EXTRA_SYMBOLS=…/aic_load_fw/Module.symvers`. `aic_btusb` si compila con
  `CONFIG_PLATFORM_UBUNTU=y` (nome storico: seleziona il percorso del firmware con sottodirectory,
  è il default di tutti e tre i Makefile).
- Dopo aver cambiato i moduli su un sistema con `boot.extraModulePackages` serve un riavvio
  (`modprobe` legge `/run/booted-system`); per provare una modifica senza riavviare, `rmmod`/`insmod`.

## Convenzioni

Italiano per commit e documentazione; commit atomici; niente refactor non richiesti; commenti
solo per un «perché» non ovvio. Commit che terminano con la riga `Co-Authored-By`; messaggi con
backtick in un file e `git commit -F`.

## Brief per gli agenti successivi (Bluetooth, Wi-Fi/coesistenza, CLI Rust)

- Non toccare: `install.sh`/`flake.nix`/`nix/` e il Makefile radice se non serve; la sequenza di
  caricamento (`aic_load_fw` prima di `aic8800_fdrv`); il `softdep` di btusb; il firmware in
  `firmware/`. Ogni modifica ai sorgenti C va riprovata con `nix build .#driver-6_12 .#driver-6_6
  .#driver-6_1 .#driver-7_2` (almeno) oltre che sul kernel di ohos.
- Una firma che cambia tra kernel: sonda sull'header nel Makefile radice (vedi sopra).
- Moduli in `/sys/module/<nome>/parameters`; debugfs di fdrv solo con `CONFIG_DEBUG_FS`.
- Su ohos la chiavetta è anche l'unico Bluetooth (cassa «Eubiq BTS»): dopo ogni prova controllare
  `bluetoothctl show` e `nmcli device`; `eno1` mantiene la rete.
