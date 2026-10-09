# Driver WiFi6 + Bluetooth per chiavetta USB AIC8800M80

Pacchetto autonomo (sorgenti incluse, nessuna dipendenza da repository esterni
a install-time) per far funzionare la chiavetta USB dual-band WiFi6
(802.11ax) + Bluetooth basata sul chip **AIC8800M80** ("AIC", VID:PID
iniziale `1111:1111`, si presenta come lettore CD-ROM/USB Mass Storage finché
non viene attivata in modalità di rete).

La configurazione invia la sequenza SCSI proprietaria `F3` → `F2`, seguita
da un reset USB (`ResetUSB=1`) per forzare la nuova enumerazione. Sulla
chiavetta `bcdDevice=10.01` di questa macchina, sulle porte `usb4` i comandi
da soli lasciavano visibile `1111:1111`; aggiungendo il reset il collegamento
automatico ha raggiunto `a69c:8d81`, con Wi-Fi e Bluetooth disponibili.

Testato su Ubuntu, kernel `7.0.0-30-generic` e `7.0.0-31-generic`.

## Perché questo pacchetto

Il repository upstream (`radxa-pkg/aic8800` + installer `olamellberg/AIC8800D80`)
**non compila as-is** su kernel recenti: le patch incluse nel repo per le
versioni 7.1/7.2 del kernel non si applicano (path mismatch, restano
`(skipped - does not apply at -p5 or -p6)`), e mancano fix per API rimosse
dal kernel. Le sorgenti in questo pacchetto sono già corrette e verificate
con una build completa e funzionante (WiFi + Bluetooth operativi).

Fix applicati rispetto all'upstream:

1. **`dkms-src/aic8800_fdrv/rwnx_rx.c`** — `in_irq()` è stata rimossa dai
   kernel recenti (sostituita da `in_hardirq()`); causava un errore di
   compilazione (`implicit declaration of function 'in_irq'`).
2. **`dkms-src/aic8800_fdrv/rwnx_main.c`** — `cfg80211_ops.get_tx_power` ha
   guadagnato un nuovo parametro `int radio_idx` (dopo `wdev`, prima di
   `link_id`) in questa versione del kernel; la firma della funzione del
   driver non combaciava più con quella attesa da `struct cfg80211_ops`.
3. **`dkms-src/aic_btusb/`** — sorgente del driver Bluetooth ricostruita da
   zero partendo dal repo Radxa: `CONFIG_BLUEDROID` impostato a `0` (modalità
   BlueZ invece di Android BlueDroid) e aggiunto l'ID USB post-switch
   `a69c:8d83` alla tabella dei device supportati. Compila pulita su questo
   kernel senza bisogno delle due patch di compatibilità citate
   dall'installer upstream (`fix-aic_btusb.patch`,
   `fix-aic_btusb-implicit-declare-compat_ptr.patch`), che nel repo attuale
   non esistono più con quel nome.
4. **`dkms-src/dkms.conf`** — tutti e tre i moduli (`aic_load_fw`,
   `aic8800_fdrv`, `aic_btusb`) sono registrati in **un unico pacchetto
   DKMS**, invece che con `aic_btusb` compilato e copiato a mano come faceva
   l'installer upstream. Serviva anche aggiungere `KBUILD_EXTRA_SYMBOLS` al
   comando di build di `aic8800_fdrv`, che dipende da simboli esportati da
   `aic_load_fw` (`get_testmode`, `get_fw_path`, `aicwf_prealloc_txq_alloc`,
   ecc.): senza, `modpost` falliva con "undefined!" su questo kernel (che
   tratta i simboli non risolti come errore fatale, non solo warning).

## Aggiornamenti automatici

Tutto il pacchetto è pensato per restare funzionante da solo dopo gli
aggiornamenti di sistema, senza dover rilanciare `install.sh`:

- I tre moduli sono un **unico pacchetto DKMS** (`aic8800/radxa`) con
  `AUTOINSTALL="yes"`: ad ogni nuovo kernel installato (anche
  automaticamente, via Software Updater / `unattended-upgrades`), DKMS li
  ricompila e reinstalla da solo tramite il proprio hook
  `kernel_postinst.d`.
- Perché questo funzioni, servono gli **header del kernel corrispondente**
  al momento dell'aggiornamento: `install.sh` installa il meta-pacchetto
  `linux-headers-<flavor>` (es. `linux-headers-generic`) invece della
  versione fissata a un singolo kernel, cosicché ogni nuovo kernel installato
  da apt porti automaticamente con sé gli header giusti.
- `install.sh` verifica anche che `unattended-upgrades` sia installato e
  attivo (lo abilita se mancante), che è il meccanismo che fa scattare gli
  aggiornamenti automatici di sistema (kernel, header, `dkms`,
  `usb-modeswitch`, ecc. — tutti pacchetti apt normali, già coperti dagli
  aggiornamenti di sicurezza automatici standard di Ubuntu).

Non c'è più bisogno di intervento manuale dopo un aggiornamento del kernel.

## Contenuto

```
install.sh              Installer (idempotente, sudo bash install.sh)
uninstall.sh             Disinstalla tutto quanto installato
dkms-src/                Un unico pacchetto DKMS con tutti e tre i driver:
  aic_load_fw/             firmware loader (necessario anche per WiFi)
  aic8800_fdrv/            driver WiFi6 (802.11ax)
  aic_btusb/                driver Bluetooth
firmware/                 Firmware binari per il chip 8800D80
usb_modeswitch/            Config per far scattare il passaggio da
                          1111:1111 (Mass Storage) alla modalità di rete reale
udev/                     Regole per attivare mode-switch e binding driver
                          automaticamente ad ogni collegamento
systemd/                  Servizio oneshot avviato da udev, con attesa di
                          2 secondi prima del mode-switch e reset USB
modprobe/                  Impedisce al driver generico `btusb` del kernel di
                          "rubare" il dispositivo prima di aic_btusb
modules-load.d/            Carica aic_btusb automaticamente al boot
```

## Installazione

```bash
sudo bash install.sh
```

Al termine, **scollega e ricollega fisicamente la chiavetta** (necessario
per far scattare il mode-switch), poi verifica:

```bash
lsusb                  # deve mostrare "a69c:8d81 AICSemi AIC 8800D80"
                        # (non più "1111:1111 Pandora International Ltd.")
ip link show            # interfaccia wlx... presente
nmcli device wifi list   # elenco reti visibili
bluetoothctl show        # controller Bluetooth "Powered: yes"
dkms status              # aic8800/radxa, ..., installed (x3 moduli)
```

## Disinstallazione

```bash
sudo bash uninstall.sh
```

## Limitazioni note

- I fix sono verificati sul kernel `7.0.0-30-generic`. Un kernel
  significativamente diverso (major bump, non solo un aggiornamento minor)
  potrebbe richiedere ulteriori aggiustamenti alle stesse API
  (`cfg80211_ops`, ecc.) — in tal caso DKMS segnalerà il fallimento della
  build nei log (`/var/lib/dkms/aic8800/radxa/build/make.log`) invece di
  fallire silenziosamente.
- Il controller AMD Raven `1022:15e0` non è risultato incompatibile:
  sulla porta `4-2` la sequenza completa con reset USB ha risolto il blocco,
  verificato con scollegamento fisico e nuovo collegamento l'11 settembre
  2026. Sul controller AMD 300 Series `1022:43bb`, la porta `1-6` funzionava
  già e la porta `1-9` è stata verificata con la nuova configurazione, anche
  con connessione Wi-Fi attiva. La regola non dipende dal numero della porta;
  le altre prese fisiche non sono state provate singolarmente.
  Gli errori di upload osservati durante gli scollegamenti non bastano
  da soli a diagnosticare un firmware errato o un guasto del controller.
- Il servizio può terminare con codice 0 anche senza una transizione riuscita.
  Verificare sempre `lsusb`, `nmcli device status` e `bluetoothctl show`.
  Nei test riusciti le risposte SCSI riportavano comunque `status 1` e il
  reset poteva riportare `-5`/`-4` mentre il dispositivo cambiava identità.
  I dettagli sono in `journalctl -b -u aic8800-modeswitch.service`.
