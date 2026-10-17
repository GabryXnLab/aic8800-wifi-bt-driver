{ self, runCommand }:
runCommand "aic8800-firmware" { } ''
  mkdir -p $out/lib/firmware/aic8800D80
  cp ${self}/firmware/*.bin $out/lib/firmware/aic8800D80/
''
