{ pkgs ? import <nixpkgs> {} }: with pkgs;
stdenv.mkDerivation rec {
  pname = "packagekit";
  version = "1.1.13";

  outputs = [ "out" "dev" ];

  src = ./.;

  buildInputs = [ glib polkit python3 systemd gobject-introspection bash-completion ];
  propagatedBuildInputs = [ sqlite boost nixFlakes ];
  nativeBuildInputs = [ vala intltool pkgconfig gtk-doc meson ];

  mesonFlags = [
    "-Dpackaging_backend=nix"
    "-Ddbus_sys=${placeholder "out"}/share/dbus-1/system.d"
    "-Ddbus_services=${placeholder "out"}/share/dbus-1/system-services"
    "-Dsystemdsystemunitdir=${placeholder "out"}/lib/systemd/system"
  ];

  enableParallelBuilding = true;
}
