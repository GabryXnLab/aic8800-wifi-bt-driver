{
  description = "Driver Linux per la chiavetta USB Wi-Fi 6 + Bluetooth AIC8800D80: modulo NixOS e matrice di build sui kernel";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      # Kernel su cui si verifica che i moduli compilino.
      kernels = pkgs: with pkgs.linuxKernel.packages; {
        "5_15" = linux_5_15;
        "6_1" = linux_6_1;
        "6_6" = linux_6_6;
        "6_12" = linux_6_12;
        "6_18" = linux_6_18;
        "7_2" = linux_7_2;
      };
    in
    {
      lib.mkDriver = { pkgs, kernelPackages }: import ./nix/driver.nix { inherit self pkgs kernelPackages; };

      nixosModules.default = import ./nix/module.nix self;

      packages = forAll (pkgs:
        nixpkgs.lib.mapAttrs' (name: kp: {
          name = "driver-${name}";
          value = self.lib.mkDriver { inherit pkgs; kernelPackages = kp; };
        }) (kernels pkgs));
    };
}
