{
  description = "rbx-exec — roblox executor targeting the sober runtime on linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    rust-overlay = {
      url = "github:oxalica/rust-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    naersk = {
      url = "github:nix-community/naersk";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, flake-utils, rust-overlay, naersk }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        overlays = [ (import rust-overlay) ];
        pkgs = import nixpkgs { inherit system overlays; };
        rustToolchain = pkgs.rust-bin.stable.latest.default.override {
          targets = [ "x86_64-unknown-linux-gnu" ];
          extensions = [ "rust-src" ];
        };
        naersk-lib = naersk.lib.${system}.override {
          cargo = rustToolchain;
          rustc = rustToolchain;
        };

        mingw = pkgs.pkgsCross.mingwW64;

        luau-compile = pkgs.stdenv.mkDerivation {
          pname = "luau-compile";
          version = "0.650";
          src = pkgs.fetchFromGitHub {
            owner = "luau-lang";
            repo = "luau";
            rev = "0.650";
            sha256 = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
          };
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
          cmakeFlags = [ "-DLUAU_BUILD_CLI=ON" ];
          installPhase = ''
            mkdir -p $out/bin
            cp luau-compile $out/bin/
          '';
        };

        shim64 = mingw.stdenv.mkDerivation {
          pname = "rbx-exec-shim64";
          version = "0.1.0";
          src = ./shim;
          buildPhase = ''
            x86_64-w64-mingw32-gcc \
              -shared -O2 -static-libgcc -nostdlib \
              -Wl,--entry=_DllMainCRTStartup \
              -Wl,--enable-stdcall-fixup \
              -o shim64.dll \
              shim.c pe.c ldr_unlink.c hooks.c luau_bridge.c pipe.c scan.c \
              -lkernel32 -lntdll -luser32 -lwininet
          '';
          installPhase = ''
            mkdir -p $out/lib
            cp shim64.dll $out/lib/
          '';
        };

        loader = mingw.stdenv.mkDerivation {
          pname = "rbx-exec-loader";
          version = "0.1.0";
          src = ./loader;
          buildPhase = ''
            x86_64-w64-mingw32-g++ \
              -std=c++20 -O2 -static -static-libgcc -static-libstdc++ \
              -o rbx-loader.exe \
              loader.cpp manual_map.cpp apc_inject.cpp \
              -lntdll -lkernel32
          '';
          installPhase = ''
            mkdir -p $out/bin
            cp rbx-loader.exe $out/bin/
          '';
        };

        rbx-exec-ui = naersk-lib.buildPackage {
          pname = "rbx-exec-ui";
          src = ./ui;
          nativeBuildInputs = with pkgs; [ pkg-config wrapGAppsHook4 ];
          buildInputs = with pkgs; [
            gtk4 glib pango cairo gdk-pixbuf graphene
            webkitgtk_6_0 openssl
          ];
        };

      in {
        packages = {
          inherit shim64 loader rbx-exec-ui luau-compile;

          default = pkgs.stdenv.mkDerivation {
            pname = "rbx-exec";
            version = "0.1.0";
            src = ./.;
            nativeBuildInputs = [ pkgs.makeWrapper ];
            installPhase = ''
              mkdir -p $out/{bin,lib/rbx-exec,share/rbx-exec}
              cp ${shim64}/lib/shim64.dll     $out/lib/rbx-exec/
              cp ${loader}/bin/rbx-loader.exe $out/lib/rbx-exec/
              cp ${luau-compile}/bin/luau-compile $out/lib/rbx-exec/
              cp -r offsets                    $out/share/rbx-exec/
              cp scripts/rbx-exec              $out/bin/rbx-exec
              chmod +x $out/bin/rbx-exec
              wrapProgram $out/bin/rbx-exec \
                --prefix PATH : ${pkgs.lib.makeBinPath [
                  pkgs.flatpak pkgs.coreutils pkgs.gnugrep
                  rbx-exec-ui
                ]} \
                --set RBX_EXEC_LIB  $out/lib/rbx-exec \
                --set RBX_EXEC_DATA $out/share/rbx-exec \
                --set RBX_EXEC_UI   ${rbx-exec-ui}/bin/rbx-exec-ui
            '';
          };
        };

        devShells.default = pkgs.mkShell {
          buildInputs = [
            rustToolchain
            mingw.buildPackages.gcc
            pkgs.pkg-config pkgs.gtk4 pkgs.webkitgtk_6_0 pkgs.openssl
            pkgs.cmake pkgs.ninja pkgs.python3
          ];
          shellHook = ''
            export RBX_EXEC_DEV=1
            echo "rbx-exec dev shell — mingw + rust + gtk4 ready"
          '';
        };

        nixosModules.default = { config, lib, pkgs, ... }:
          with lib; let cfg = config.programs.rbx-exec; in {
            options.programs.rbx-exec = {
              enable = mkEnableOption "rbx-exec roblox executor";
              package = mkOption {
                type = types.package;
                default = self.packages.${system}.default;
              };
            };
            config = mkIf cfg.enable {
              environment.systemPackages = [
                cfg.package
                pkgs.flatpak
              ];
              services.flatpak.enable = true;
              xdg.portal = {
                enable = true;
                extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
              };
            };
          };
      });
}
