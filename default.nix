/*

How to use?
***********

If you have Nix installed, you can get an environment with everything
needed to compile file-roller by running:

    $ nix-shell

at the root of the file-roller repository.

You can also compile file-roller and ‘install’ it by running:

    $ nix-build

at the root of the file-roller repository. The command will install
file-roller to a location under Nix store and create a ‘result’ symlink
in the current directory pointing to the in-store location.

The dependencies are pinned, you can update them to latest versions with:

    $ nix-shell --run 'niv update'

How to tweak default arguments?
*******************************

Nix supports the ‘--arg’ option (see nix-build(1)) that allows you
to override the top-level arguments.

For instance, to use your local copy of Nixpkgs:

    $ nix-build --arg pkgs "import $HOME/Projects/nixpkgs {}"

Or to speed up the build by not running the test suite:

    $ nix-build --arg doCheck false

*/

{
  # Nixpkgs instance, will default to one from Niv.
  pkgs ? null,
  # Whether to run tests when building File Roller using nix-build.
  doCheck ? true,
  # Whether to build File Roller, or shell for developing it.
  # We do not use lib.inNixShell because that would also apply
  # when in a nix-shell of some package depending on this one.
  shell ? false,
} @ args:

let
  # Pinned Nix dependencies (e.g. Nixpkgs) managed by Niv.
  sources = import ./nix/sources.nix;

  # Setting pkgs to the pinned version
  # when not overridden in args.
  pkgs =
    if args.pkgs or null == null
    then
      import sources.nixpkgs {
        overlays = [];
        config = {};
      }
    else args.pkgs;

  inherit (pkgs) lib;

  # Function for building File Roller or shell for developing it.
  makeDerivation =
    if shell
    then pkgs.mkShell
    else pkgs.stdenv.mkDerivation;
in
makeDerivation rec {
  name = "file-roller";

  src =
    let
      # Do not copy to the store:
      # - build directory, since Nixpkgs’ Meson infrastructure would want to use it
      # - .git directory, since it would unnecessarily bloat the source
      cleanSource = path: _: !lib.elem (builtins.baseNameOf path) ["build" ".git"];
    in
    if shell
    then null
    else builtins.filterSource cleanSource ./.;

  # Dependencies for build platform
  nativeBuildInputs = with pkgs; [
    desktop-file-utils
    gettext
    gi-docgen
    glibcLocales
    gobject-introspection
    itstool
    libxml2
    meson
    ninja
    pkg-config
    wrapGAppsHook4
  ] ++ lib.optionals shell [
    niv
  ];

  # Dependencies for host platform
  buildInputs = with pkgs; [
    cpio
    file
    glib
    adwaita-icon-theme
    nautilus
    gtk4
    json-glib
    libarchive
    libadwaita
    libportal-gtk4
  ];

  mesonFlags = [
    "-Dintrospection=enabled"
    "-Dapi_docs=enabled"
  ];

  inherit doCheck;
}
