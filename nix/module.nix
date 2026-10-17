self:
{ config, lib, pkgs, ... }:

let
  cfg = config.hardware.aic8800;
  driver = import ./driver.nix {
    inherit self pkgs;
    kernelPackages = config.boot.kernelPackages;
  };
in
{
  options.hardware.aic8800.enable = lib.mkEnableOption "la chiavetta USB Wi-Fi 6 + Bluetooth AIC8800D80 (88M80)";

  config = lib.mkIf cfg.enable {
    boot.extraModulePackages = [ driver ];
    environment.systemPackages = [ (pkgs.callPackage ./aicctl.nix { inherit self; }) ];

    # btusb generico non sa inizializzare il controller BT AIC (timeout su HCI_Reset):
    # aic_btusb deve prendersi il dispositivo prima.
    boot.extraModprobeConfig = builtins.readFile "${self}/modprobe/aic8800-bt.conf";

    # Da 1111:1111 (Mass Storage) ad a69c:8d81 (Wi-Fi + BT). Lanciato come servizio e non da
    # RUN di udev: su questa revisione della chiavetta RUN arriva troppo presto.
    services.udev.extraRules = ''
      ACTION=="add", SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="1111", ATTR{idProduct}=="1111", TAG+="systemd", ENV{SYSTEMD_WANTS}+="aic8800-modeswitch.service"
    '';
    systemd.services.aic8800-modeswitch = {
      description = "AIC8800D80 USB mode switch";
      serviceConfig = {
        Type = "oneshot";
        ExecStartPre = "${pkgs.coreutils}/bin/sleep 2";
        ExecStart = "${pkgs.usb-modeswitch}/bin/usb_modeswitch -c ${self}/usb_modeswitch/1111_1111";
        TimeoutStartSec = 30;
      };
    };

    hardware.bluetooth.enable = lib.mkDefault true;
  };
}
