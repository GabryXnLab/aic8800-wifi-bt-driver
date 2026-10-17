#!/bin/bash
# install.sh - Driver Wi-Fi 6 (802.11ax) + Bluetooth per la chiavetta USB AIC8800D80
# ("88M80", clone "Pandora": si presenta come 1111:1111 Mass Storage finché non switchata).
#
# Funziona su Debian/Ubuntu, Fedora/RHEL, Arch, openSUSE (gestore pacchetti rilevato da
# /etc/os-release); altrove installa a mano dkms, make, gcc, header del kernel e usb_modeswitch
# e rilancia con AIC_SKIP_DEPS=1. Su NixOS non serve: usa il modulo del flake (vedi CLAUDE.md).
#
# I tre moduli (aic_load_fw, aic8800_fdrv, aic_btusb) sono UN UNICO pacchetto DKMS: dopo un
# aggiornamento del kernel vengono ricompilati da soli (servono gli header di quel kernel).
#
# Uso: sudo bash install.sh        (AIC_SKIP_DEPS=1 salta l'installazione dei pacchetti)

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KVER=$(uname -r)

echo "=========================================="
echo " AIC8800D80 Wi-Fi 6 + Bluetooth - Installer"
echo "=========================================="
echo "Kernel: $KVER"
echo

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Errore: esegui con sudo.${NC}"
    exit 1
fi

. /etc/os-release 2>/dev/null || true
DISTRO_IDS=" ${ID:-} ${ID_LIKE:-} "
has_id() { [[ "$DISTRO_IDS" == *" $1 "* ]]; }

if has_id nixos; then
    echo -e "${YELLOW}NixOS: non si usa questo script. Nel flake: inputs.aic8800, e in configuration.nix${NC}"
    echo -e "${YELLOW}  imports = [ inputs.aic8800.nixosModules.default ]; hardware.aic8800.enable = true;${NC}"
    exit 1
fi

STEP=1
TOTAL=6

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Prerequisiti...${NC}"

install_deps() {
    local flavor="${KVER##*-}" pkgbase
    if has_id debian || has_id ubuntu; then
        local pk="dkms build-essential usb-modeswitch usb-modeswitch-data"
        # Meta-pacchetto degli header (es. linux-headers-generic): ogni kernel nuovo porta i suoi.
        dpkg -s "linux-headers-$flavor" &>/dev/null || pk="$pk linux-headers-$flavor"
        [ -d "/lib/modules/$KVER/build" ] || pk="$pk linux-headers-$KVER"
        apt-get update -qq && apt-get install -y $pk
    elif has_id fedora || has_id rhel || has_id centos; then
        dnf install -y dkms gcc make usb_modeswitch "kernel-devel-$KVER" || dnf install -y dkms gcc make usb_modeswitch kernel-devel
    elif has_id arch; then
        pkgbase=$(cat "/usr/lib/modules/$KVER/pkgbase" 2>/dev/null || echo linux)
        pacman -S --needed --noconfirm dkms base-devel usb_modeswitch "$pkgbase-headers"
    elif has_id suse || has_id opensuse; then
        zypper --non-interactive install dkms gcc make usb_modeswitch "kernel-$flavor-devel"
    else
        echo -e "${YELLOW}  Distribuzione non riconosciuta (${ID:-?}): installa dkms, make, gcc,${NC}"
        echo -e "${YELLOW}  gli header del kernel $KVER e usb_modeswitch, poi rilancia con AIC_SKIP_DEPS=1.${NC}"
        exit 1
    fi
}

if [ -n "$AIC_SKIP_DEPS" ]; then
    echo "  -> Saltati (AIC_SKIP_DEPS)."
else
    install_deps
fi
[ -d "/lib/modules/$KVER/build" ] || { echo -e "${RED}Errore: mancano gli header del kernel $KVER.${NC}"; exit 1; }
command -v dkms &>/dev/null || { echo -e "${RED}Errore: dkms non installato.${NC}"; exit 1; }
STEP=$((STEP + 1))

###############################################################################
echo -e "${GREEN}[$STEP/$TOTAL] Driver Wi-Fi + Bluetooth (DKMS: aic8800/radxa)...${NC}"

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
echo "  -> AUTOINSTALL=yes: si ricompilano da soli ad ogni nuovo kernel installato,"
echo "     a patto che ne siano presenti gli header."
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
if command -v mokutil &>/dev/null && mokutil --sb-state 2>/dev/null | grep -q enabled; then
    echo -e "${YELLOW}Secure Boot attivo: i moduli devono essere firmati (chiave MOK di DKMS,${NC}"
    echo -e "${YELLOW}su Ubuntu/Fedora la procedura è guidata; altrove vedi la documentazione di dkms).${NC}"
fi
echo "Stato DKMS:"
dkms status | grep aic8800 || true
