# Build out-of-tree dei tre moduli, nell'ordine richiesto dai simboli esportati da aic_load_fw.
#   make [KVER=<release>] [KDIR=<albero build del kernel>] [AIC_FW_DIR=<dir firmware>]
#   make install   # modules_install in updates/aic8800 + depmod
KVER       ?= $(shell uname -r)
KDIR       ?= /lib/modules/$(KVER)/build
AIC_FW_DIR ?= /lib/firmware
HERE       := $(CURDIR)

# Un kernel compilato con Clang (CachyOS, Android, ...) rifiuta i moduli fatti con GCC.
ifeq ($(origin CC),default)
ifneq ($(shell grep -s '^CONFIG_CC_IS_CLANG=y' $(KDIR)/.config),)
KOPTS += LLVM=1
endif
endif

# Alcuni cambi di API di cfg80211 sono retroportati nei kernel stabili (es. 6.12.x): il numero di
# versione non basta, si guarda l'header. Il kernel non ha nulla di meglio, è ciò che fa autoconf.
CFG80211_H := $(firstword $(wildcard $(KDIR)/include/net/cfg80211.h $(KDIR)/source/include/net/cfg80211.h))
ifneq ($(and $(CFG80211_H),$(shell tr -s '[:space:]' ' ' < $(CFG80211_H) | grep -E 'set_monitor_channel.{2}struct wiphy [*]wiphy, struct net_device')),)
AIC_CFLAGS += -DAIC_MONITOR_CHAN_HAS_DEV
endif
ifneq ($(and $(CFG80211_H),$(shell tr -s '[:space:]' ' ' < $(CFG80211_H) | grep -E 'get_tx_power.{2}struct wiphy [*]wiphy, struct wireless_dev [*]wdev, int radio_idx')),)
AIC_CFLAGS += -DAIC_GET_TX_POWER_HAS_RADIO
endif

KMAKE = $(MAKE) -C $(KDIR) $(KOPTS) AIC_FW_DIR=$(AIC_FW_DIR) KCFLAGS="$(AIC_CFLAGS)"

.PHONY: modules install clean

modules:
	$(KMAKE) M=$(HERE)/aic_load_fw modules
	$(KMAKE) M=$(HERE)/aic8800_fdrv modules KBUILD_EXTRA_SYMBOLS=$(HERE)/aic_load_fw/Module.symvers
	$(KMAKE) M=$(HERE)/aic_btusb modules

install: modules
	for m in aic_load_fw aic8800_fdrv aic_btusb; do \
		$(KMAKE) M=$(HERE)/$$m modules_install INSTALL_MOD_DIR=updates/aic8800 || exit 1; \
	done
	depmod -a $(KVER)

clean:
	for m in aic_load_fw aic8800_fdrv aic_btusb; do \
		$(KMAKE) M=$(HERE)/$$m clean; \
	done
