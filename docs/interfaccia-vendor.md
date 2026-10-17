# Interfaccia vendor nl80211 di `aic8800_fdrv`

ABI stabile per userspace (CLI `aicctl`, servizi di ottimizzazione). Definita in
`dkms-src/aic8800_fdrv/aic_vendor.h` (`enum aic_vendor_*`). Si estende solo aggiungendo
subcmd/attributi in coda: i numeri esistenti non cambiano.

## Trasporto

- Famiglia generic netlink `nl80211`, comando `NL80211_CMD_VENDOR`, **OUI `0x414943`**
  (i byte ASCII «AIC»; non e' un OUI registrato, serve solo a non collidere con quelli
  Android `0x001A11` e Broadcom `0x001018`, gia' presenti nel driver per l'HAL Android).
- Dati: attributi netlink piatti (non annidati) in `NL80211_ATTR_VENDOR_DATA`; intestazione
  `{u16 len, u16 type}` little-endian su x86, payload allineato a 4 byte. Le risposte hanno la
  stessa forma. Tutti i numeri sono nell'endianness dell'host.
- Permessi: `CAP_NET_ADMIN` (nl80211 li richiede per ogni comando vendor).
- Si indirizza il wiphy (`iw phy phyN vendor ...`) o l'interfaccia (`iw dev wlan0 vendor ...`).
  `GET_STATS` richiede l'interfaccia aperta (`NEED_NETDEV|NEED_RUNNING`).
- Gli attributi di un comando con dati non vengono validati da una policy del kernel (`iw
  vendor send` non marca i dati come annidati e il kernel li rifiuterebbe): il driver controlla
  lunghezze e valori e risponde `-EINVAL`.
- Il nome `phyN` cambia a ogni ricarica del modulo: cercarlo con `iw dev`.

## Comandi

| subcmd | nome | dati in ingresso | risposta |
|---|---|---|---|
| `0x1` | `COEX_GET` | nessuno | attributi coex (sotto) |
| `0x2` | `COEX_SET` | zero o piu' attributi coex 1-7; quelli omessi restano invariati | stato coex risultante |
| `0x3` | `GET_INFO` | nessuno | versioni, chip, temperatura |
| `0x4` | `GET_STATS` | nessuno | contatori netdev, canale, stazione |

### Attributi

| id | nome | tipo | in/out | note |
|---|---|---|---|---|
| 1 | `COEX_BT_ON` | u8 0/1 | set/get | |
| 2 | `COEX_DISABLE_COEXNULL` | u8 0/1 | set/get | |
| 3 | `COEX_ENABLE_NULLCTS` | u8 0/1 | set/get | |
| 4 | `COEX_ENABLE_PERIODIC_TIMER` | u8 0/1 | set/get | |
| 5 | `COEX_TIMESLOT_SET` | u8 0/1 | set/get | abilita i due timeslot |
| 6 | `COEX_TIMESLOT0` | u32 | set/get | |
| 7 | `COEX_TIMESLOT1` | u32 | set/get | |
| 8 | `COEX_APPLIED` | u8 | get | 1 se il firmware ha ricevuto il valore dall'ultimo avvio |
| 9 | `DRIVER_VERSION` | stringa | get | |
| 10 | `FW_VERSION` | stringa | get | stringa del firmware (data di build + hash) |
| 11 | `CHIP_ID` | u8 | get | `PRODUCT_ID_*` di `aicwf_usb.h` (5 = AIC8800D81, la D80 post-switch) |
| 12 | `BAND_5G` | u8 | get | |
| 13 | `IF_RUNNING` | u8 | get | interfacce aperte; 0 = firmware non avviato |
| 14 | `TEMP_C` | s8 | get | gradi C; assente se firmware spento o non risponde |
| 15-17 | `CHAN_FREQ`, `CHAN_CENTER_FREQ1`, `CHAN_WIDTH` | u32 MHz | get | solo con un canale in uso (associata, AP, monitor) |
| 18-25 | `NET_{RX,TX}_{PACKETS,BYTES,ERRORS,DROPPED}` | u64 | get | da `dev_get_stats` |
| 26 | `STA_RSSI` | s8 dBm | get | solo se associata |
| 27 | `STA_RATE_INFO` | u32 | get | grezzo (`union rwnx_rate_ctrl_info`) |
| 28-33 | `STA_TX_FAILED`, `STA_CHAN_TIME`, `STA_CHAN_BUSY_TIME`, `STA_CHAN_TX_BUSY_TIME`, `STA_ACK_FAIL`, `STA_ACK_SUCC` | u32 | get | contatori del firmware per la stazione AP corrente |
| 34 | `PAD` | - | - | padding dei u64 (`nla_put_u64_64bit`) |

`COEX_SET` accetta solo 0/1 per i flag e rifiuta il resto con `-EINVAL` senza toccare lo stato.

## Coesistenza Wi-Fi/Bluetooth

Il firmware riceve `MM_SET_COEX_REQ` e non ha un comando di lettura: il driver **ricorda
l'ultimo valore inviato** per wiphy. `COEX_APPLIED=0` significa che e' il valore che verra' inviato
al prossimo avvio del firmware (prima apertura dell'interfaccia); un `COEX_SET` a firmware spento
lo memorizza soltanto. Il valore va rimandato dopo ogni ricarica del modulo o reset della
chiavetta (lo stato torna ai parametri del modulo).

Parametri del modulo `aic8800_fdrv` (sola lettura, valori di partenza a ogni avvio):
`coex_bt_on=1`, `coex_disable_coexnull=0`, `coex_enable_nullcts=1`, `coex_enable_periodic_timer=0`,
`coex_timeslot_set=0`, `coex_timeslot0=0`, `coex_timeslot1=0`. I default riproducono il
comportamento precedente (`disable_coexnull=0, enable_nullcts=1`, timeslot spenti).

**Significato dei timeslot: non documentato** (nessuna unita' ne' effetto noto nei sorgenti).
Il firmware accetta valori arbitrari senza errore. Un effetto misurabile non e' stato trovato:
vedi «Misure» sotto. Trattare `COEX_TIMESLOT_SET=1` come sperimentale.

## Eventi

Gruppo multicast `vendor` di nl80211 (`iw event` li mostra; `subcmd` = indice dell'evento, nessun
attributo oggi):

| subcmd | evento | quando |
|---|---|---|
| 0 | `COEX_TS_TIMEOUT` | il firmware segnala `SM_COEX_TS_TIMEOUT_IND` |
| 1 | `FW_PANIC` | `MM_FW_PANIC_IND` (dettagli del crash nel log del kernel) |
| 2 | `FW_ASSERT` | `MM_FW_ASSERT_IND` |

## CLI `aicctl`

`tools/aicctl` implementa l'ABI senza `iw`: `info`, `stats`, `coex get`, `coex set`, `events`, con `--json`
(un oggetto con i nomi della colonna «nome» in minuscolo, piu' `phy`; per `events` un oggetto per riga:
`time`, `phy`, `event`, `subcmd`) e `--dev wlan0|phyN`. Errori: `EPERM` = serve root, `ENETDOWN` = interfaccia chiusa
(firmware non avviato), `EOPNOTSUPP` = modulo senza interfaccia vendor, `EINVAL` = valore rifiutato.
`coex set` valida i booleani lato CLI; per provare il rifiuto del kernel serve `iw ... send` con un valore 2.

## Esempi

Con `iw` serve sempre un argomento dati: `/dev/null` per «nessuno». `recv` stampa la risposta in
esadecimale, `recvbin` in binario.

```bash
V="iw dev wlan0 vendor"
sudo $V recv 0x414943 0x3 /dev/null          # GET_INFO
sudo $V recv 0x414943 0x1 /dev/null          # COEX_GET
sudo $V recvbin 0x414943 0x4 /dev/null | xxd # GET_STATS

# COEX_SET enable_nullcts=0 : attributo {len=5, type=3, valore=0} + 3 byte di padding
sudo $V send 0x414943 0x2 0x05 0x00 0x03 0x00 0x00 0x00 0x00 0x00
# COEX_SET timeslot0=10000 (0x2710): {len=8, type=6, u32}
sudo $V send 0x414943 0x2 0x08 0x00 0x06 0x00 0x10 0x27 0x00 0x00
```

`send` non stampa la risposta: per leggerla dopo un set usare `recv` con lo stesso subcmd.

## Cosa non e' esposto (e perche')

- **Scritture efuse/flash e i ~57 comandi RF/calibrazione**: restano solo nell'interfaccia
  privata storica `SIOCDEVPRIVATE+1` (`aic_priv_cmd.c`), senza protezioni nuove. Non sono stati
  portati nell'ABI vendor: quando lo saranno, le scritture devono stare dietro un parametro di
  modulo spento di default.
- **debugfs** (`rwnx_debugfs.c`) resta spento (`CONFIG_DEBUG_FS ?= n`): contiene scritture
  arbitrarie sulla memoria del firmware e sui registri, e non e' stato verificato sulla matrice.
  Le statistiche utili passano dai comandi sopra.
- **Statistiche per stazione/RSSI/rate/survey standard**: restano da `cfg80211`
  (`iw dev wlan0 station dump`, `iw dev wlan0 survey dump`).

## Misure (ohos, kernel 7.2.9-cachyos, `wlan0` non associata)

- `GET_INFO`, `COEX_GET/SET`, `GET_STATS` verificati; `TEMP_C` = 44-45 C a riposo.
- Durata di `iw dev wlan0 scan` (5 BSS visti): 1,56-1,57 s con i default, 1,58-1,60 s con
  `enable_nullcts=0`, 1,57-1,61 s con `disable_coexnull=1`, `periodic_timer=1` e timeslot
  10000/10000 o 2000/18000 (scansioni non riuscite per collisione con quelle di NetworkManager
  escluse). Nessun errore del firmware ne' evento `COEX_TS_TIMEOUT`.
- **Non misurato**: throughput e latenza Wi-Fi (nessuna rete nota), continuita' A2DP sotto
  carico Wi-Fi, effetto reale di timeslot e periodic timer, eventi `FW_PANIC/ASSERT`.
