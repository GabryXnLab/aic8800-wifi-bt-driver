# Moduli del kernel contro un dato `kernelPackages`; il firmware è quello dello store.
{ self, pkgs, kernelPackages }:

let
  inherit (pkgs) lib;
  kernel = kernelPackages.kernel;
  firmware = pkgs.callPackage ./firmware.nix { inherit self; };
in
pkgs.stdenv.mkDerivation {
  pname = "aic8800";
  version = "${self.shortRev or "dirty"}-${kernel.version}";
  src = self + "/dkms-src";

  nativeBuildInputs = kernel.moduleBuildDependencies ++ [ pkgs.removeReferencesTo ];
  hardeningDisable = [ "pic" "format" ];

  # I driver cercano il firmware con un percorso (filp_open), e /lib/firmware su NixOS non esiste.
  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES \
      KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build KVER=${kernel.modDirVersion} \
      AIC_FW_DIR=${firmware}/lib/firmware \
      KOPTS="${lib.escapeShellArgs kernelPackages.kernelModuleMakeFlags}"
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    dest=$out/lib/modules/${kernel.modDirVersion}/updates/aic8800
    mkdir -p $dest
    cp aic_load_fw/aic_load_fw.ko aic8800_fdrv/aic8800_fdrv.ko aic_btusb/aic_btusb.ko $dest/
    # __FILE__ degli header del kernel nei messaggi di WARN: senza, la closure si porta
    # dietro kernel.dev e tutta la toolchain (+2,7 GiB).
    remove-references-to -t ${kernel.dev} $dest/*.ko
    runHook postInstall
  '';
}
