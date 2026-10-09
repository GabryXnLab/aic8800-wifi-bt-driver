#!/bin/bash
# uninstall.sh - Rimuove il driver AIC8800M80 WiFi+Bluetooth installato da install.sh
#
# Uso: sudo bash uninstall.sh

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Errore: esegui con sudo."
    exit 1
fi

KVER=$(uname -r)

echo "Scarico i moduli..."
rmmod aic_btusb 2>/dev/null || true
rmmod aic8800_fdrv 2>/dev/null || true
rmmod aic_load_fw 2>/dev/null || true

echo "Rimuovo i driver (WiFi + Bluetooth) da DKMS..."
dkms remove aic8800/radxa --all 2>/dev/null || true
rm -rf /usr/src/aic8800-radxa

# Residuo di eventuali versioni precedenti di install.sh (aic_btusb non-DKMS)
rm -f "/lib/modules/$KVER/kernel/drivers/bluetooth/aic_btusb.ko"
depmod -a "$KVER"

echo "Rimuovo le configurazioni..."
rm -f "/etc/usb_modeswitch.d/1111:1111"
rm -f /etc/udev/rules.d/41-aic8800d80-modeswitch.rules
rm -f /etc/systemd/system/aic8800-modeswitch.service
rm -f /etc/modprobe.d/aic8800-bt.conf
rm -f /etc/modules-load.d/aic8800.conf
systemctl daemon-reload
udevadm control --reload-rules

echo "Rimuovo il firmware..."
rm -rf /lib/firmware/aic8800D80

echo "Fatto. Riavvia o ricollega la chiavetta per completare la rimozione."
