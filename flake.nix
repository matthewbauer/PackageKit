{

  description = "a D-Bus abstraction layer that allows the session user to manage packages in a secure way using a cross-distro, cross-architecture API";

  inputs.make-package.url = "github:matthewbauer/make-package.nix";
  inputs.nix.url = "github:matthewbauer/nix?ref=make-package-flake";

  outputs = { self, make-package, nix }: make-package.makePackagesFlake {
    defaultPackageName = "PackageKit";
  } {
    inherit nix;

    PackageKit = { stdenv, ... }: rec {
      pname = "packagekit";
      version = "1.1.13";

      outputs = [ "out" "dev" ];

      depsBuildBuild = [
        "pkgconfig"
      ];
      depsBuildHost = [
        "vala"
        "intltool"
        "pkgconfig"
        "gtk-doc"
        "meson"
        "glib"
        "gobject-introspection"
        "libxslt"
        "ninja"
      ];
      depsHostTarget = [
        "glib"
        "polkit"
        "python3"
        "systemd"
        "gobject-introspection"
        "bash-completion"
        "gst_all_1.gstreamer"
        "gst_all_1.gst-plugins-base"
        "gtk3"
        "nlohmann_json"
      ];
      depsHostTargetPropagated = [
        "sqlite"
        "boost"
        "nix"
      ];

      src = self;

      mesonFlags = [
        "-Dpackaging_backend=nix"
        "-Dlocal_checkout=true"
        "-Dman_pages=false"
        "-Dbash_completion=false"
        "-Ddbus_sys=${placeholder "out"}/share/dbus-1/system.d"
        "-Ddbus_services=${placeholder "out"}/share/dbus-1/system-services"
        "-Dsystemdsystemunitdir=${placeholder "out"}/lib/systemd/system"
      ];
    };
  };
}
