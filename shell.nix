{
  pkgs ? null,
  doCheck ? true,
}:
import ./default.nix {
  inherit doCheck pkgs;
  shell = true;
}
