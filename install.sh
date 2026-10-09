#!/bin/bash
# install.sh - Driver WiFi6 (802.11ax) + Bluetooth per chiavetta USB AIC8800M80
# ("Pandora" clone, si presenta come 1111:1111 Mass Storage finché non switchata).
#
# Sorgenti già corrette e verificate funzionanti su questa macchina (kernel
# 7.0.0-30-generic): il repo upstream (radxa-pkg/aic8800 + olamellberg/AIC8800D80)
# non compilava as-is su questo kernel, vedi README.md per il dettaglio dei fix.
#
# Tutti e tre i moduli (aic_load_fw, aic8800_fdrv, aic_btusb) sono registrati
# come UN UNICO pacchetto DKMS: dopo un aggiornamento del kernel (anche
# automatico, via Software Updater / unattended-upgrades) vengono ricompilati
# e reinstallati da soli, senza bisogno di rilanciare questo script.
#
# Uso: sudo bash install.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KVER=$(uname -r)
FLAVOR="${KVER##*-}"   # es. "generic"

echo "=========================================="
echo " AIC8800M80 WiFi6 + Bluetooth - Installer"
echo "=========================================="
echo "Kernel: $KVER"
echo

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Errore: esegui con sudo.${NC}"
    exit 1
fi

STEP=1
TOTAL=8

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Prerequisiti...${NC}"

# Header del kernel: installiamo il meta-pacchetto "linux-headers-$FLAVOR"
# (non la versione fissa) così che ad ogni nuovo kernel installato
# dall'aggiornamento automatico vengano scaricati anche gli header giusti,
# necessari a DKMS per ricompilare i moduli senza intervento manuale.
PKGS_NEEDED=""
command -v usb_modeswitch &>/dev/null || PKGS_NEEDED="$PKGS_NEEDED usb-modeswitch usb-modeswitch-data"
command -v make &>/dev/null           || PKGS_NEEDED="$PKGS_NEEDED build-essential"
command -v dkms &>/dev/null           || PKGS_NEEDED="$PKGS_NEEDED dkms"
dpkg -s "linux-headers-$FLAVOR" &>/dev/null || PKGS_NEEDED="$PKGS_NEEDED linux-headers-$FLAVOR"
dpkg -s unattended-upgrades &>/dev/null      || PKGS_NEEDED="$PKGS_NEEDED unattended-upgrades"
if [ -n "$PKGS_NEEDED" ]; then
    apt-get update -qq && apt-get install -y $PKGS_NEEDED
else
    echo "  -> Già tutti presenti."
fi
STEP=$((STEP + 1))

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Aggiornamenti automatici di sistema...${NC}"
if grep -qs 'Unattended-Upgrade "1"' /etc/apt/apt.conf.d/20auto-upgrades 2>/dev/null; then
    echo "  -> unattended-upgrades già attivo: pacchetti apt (dkms, kernel, header,"
    echo "     usb-modeswitch, ecc.) verranno aggiornati automaticamente."
else
    echo "  -> Abilito unattended-upgrades (aggiornamenti di sicurezza automatici)..."
    dpkg-reconfigure -f noninteractive unattended-upgrades
fi
STEP=$((STEP + 1))

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Driver WiFi + Bluetooth (DKMS: aic8800/radxa)...${NC}"

# Scarico eventuali moduli caricati per evitare conflitti durante il (re)build
rmmod aic_btusb 2>/dev/null || true
rmmod btusb 2>/dev/null || true
rmmod aic8800_fdrv 2>/dev/null || true
rmmod aic_load_fw 2>/dev/null || true

# Rimuovo l'eventuale vecchio aic_btusb.ko installato manualmente (versioni
# precedenti di questo script), ora sostituito dal modulo gestito da DKMS.
rm -f "/lib/modules/$KVER/kernel/drivers/bluetooth/aic_btusb.ko"

# Reinstallazione pulita: rimuove ogni versione precedente registrata e
# ricopia esattamente la sorgente verificata contenuta in questo pacchetto.
dkms remove aic8800/radxa --all 2>/dev/null || true
rm -rf /usr/src/aic8800-radxa
cp -r "$SCRIPT_DIR/dkms-src" /usr/src/aic8800-radxa
dkms add aic8800/radxa
dkms build aic8800/radxa -k "$KVER"
dkms install aic8800/radxa -k "$KVER" --force
echo -e "  -> ${GREEN}Driver WiFi + Bluetooth installati via DKMS.${NC}"
echo "  -> AUTOINSTALL=yes: si ricompilano da soli ad ogni nuovo kernel installato"
echo "     (anche via Software Updater / unattended-upgrades), a patto che"
echo "     linux-headers-$FLAVOR sia presente per quel kernel (vedi step 1)."
STEP=$((STEP + 1))

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Firmware...${NC}"
mkdir -p /lib/firmware/aic8800D80
cp "$SCRIPT_DIR"/firmware/*.bin /lib/firmware/aic8800D80/
echo "  -> Copiati in /lib/firmware/aic8800D80/"
STEP=$((STEP + 1))

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Mode-switch (usb_modeswitch) e regole udev...${NC}"
mkdir -p /etc/usb_modeswitch.d
cp "$SCRIPT_DIR/usb_modeswitch/1111_1111" "/etc/usb_modeswitch.d/1111:1111"
cp "$SCRIPT_DIR/systemd/aic8800-modeswitch.service" /etc/systemd/system/
cp "$SCRIPT_DIR/udev/41-aic8800d80-modeswitch.rules" /etc/udev/rules.d/
systemctl daemon-reload
udevadm control --reload-rules
echo "  -> Config e servizio mode-switch installati; regole udev ricaricate."
STEP=$((STEP + 1))

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Config modprobe / autoload al boot...${NC}"
mkdir -p /etc/modprobe.d
cp "$SCRIPT_DIR/modprobe/aic8800-bt.conf" /etc/modprobe.d/
mkdir -p /etc/modules-load.d
cp "$SCRIPT_DIR/modules-load.d/aic8800.conf" /etc/modules-load.d/
echo "  -> Fatto."
STEP=$((STEP + 1))

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Ricarico i moduli...${NC}"
modprobe aic_load_fw 2>/dev/null || true
modprobe aic_btusb 2>&1 || true
rfkill unblock bluetooth 2>/dev/null || true
STEP=$((STEP + 1))

echo
echo "=========================================="
echo -e "${GREEN} Installazione completata!${NC}"
echo "=========================================="
echo
echo "Prossimo passo: scollega e ricollega fisicamente la chiavetta USB"
echo "(serve per far scattare il mode-switch da 1111:1111 alla modalità reale)."
echo
echo "Poi verifica con:"
echo "  lsusb                    # deve mostrare a69c:8d81 (non più 1111:1111)"
echo "  ip link show             # cerca l'interfaccia wlx..."
echo "  bluetoothctl show        # controller Bluetooth attivo"
echo
echo "Stato attuale moduli caricati:"
lsmod | grep -iE 'aic|bluetooth|btusb' || echo "  (nessuno - ricollega la chiavetta)"
echo
echo "Stato DKMS:"
dkms status | grep aic8800 || true
