{ self, rustPlatform }:

rustPlatform.buildRustPackage {
  pname = "aicctl";
  version = "0.1.0";
  src = self + "/tools/aicctl";
  cargoLock.lockFile = self + "/tools/aicctl/Cargo.lock";
  meta.mainProgram = "aicctl";
}
