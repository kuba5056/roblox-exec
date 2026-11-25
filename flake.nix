{
  description = "rbx-exec — roblox executor targeting the sober runtime on linux";

  inputs = {
    nixpkgs.url     = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem [ "x86_64-linux" ] (system:
      let
        pkgs  = import nixpkgs { inherit system; };
        mingw = pkgs.pkgsCross.mingwW64;

        luauSrc = pkgs.fetchFromGitHub {
          owner = "luau-lang";
          repo  = "luau";
          rev   = "0.650";
          hash  = "sha256-evIHed+y2DBSDq/g645exyIZFL6zLD+CrLWwHrRfvls=";
        };

        luau-compile = pkgs.stdenv.mkDerivation {
          pname = "luau-compile";
          version = "0.650";
          src = luauSrc;
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
          cmakeFlags = [ "-DLUAU_BUILD_CLI=ON" "-DCMAKE_BUILD_TYPE=Release" ];
          ninjaFlags = [ "Luau.Compile.CLI" ];
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp luau-compile $out/bin/
            runHook postInstall
          '';
        };

        shim64 = mingw.stdenv.mkDerivation {
          pname = "rbx-exec-shim64";
          version = "0.1.0";
          src = ./shim;
          dontUnpack = false;
          buildPhase = ''
            runHook preBuild
            ${mingw.stdenv.cc.targetPrefix}gcc \
              -shared -O2 -static-libgcc \
              -Wl,--entry=DllMainCRTStartup \
              -Wl,--enable-stdcall-fixup \
              -o shim64.dll \
              shim.c pe.c ldr_unlink.c hooks.c luau_bridge.c pipe.c scan.c \
              -lkernel32 -lntdll -luser32 -lwininet -lmsvcrt
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/lib
            cp shim64.dll $out/lib/
            runHook postInstall
          '';
          hardeningDisable = [ "all" ];
        };

        loader = mingw.stdenv.mkDerivation {
          pname = "rbx-exec-loader";
          version = "0.1.0";
          src = ./loader;
          buildPhase = ''
            runHook preBuild
            ${mingw.stdenv.cc.targetPrefix}g++ \
              -std=c++20 -O2 -static -static-libgcc -static-libstdc++ -municode \
              -o rbx-loader.exe \
              loader.cpp manual_map.cpp apc_inject.cpp \
              -lntdll -lkernel32
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp rbx-loader.exe $out/bin/
            runHook postInstall
          '';
          hardeningDisable = [ "all" ];
        };

        rustPlatform = pkgs.rustPlatform;

        rbx-exec-ui = rustPlatform.buildRustPackage {
          pname = "rbx-exec-ui";
          version = "0.1.0";
          src = ./ui;
          cargoLock.lockFile = ./ui/Cargo.lock;
          nativeBuildInputs = with pkgs; [ pkg-config wrapGAppsHook4 ];
          buildInputs       = with pkgs; [ gtk4 glib pango cairo gdk-pixbuf graphene ];
        };

      in {
        packages = {
          inherit shim64 loader rbx-exec-ui luau-compile;

          default = pkgs.stdenv.mkDerivation {
            pname = "rbx-exec";
            version = "0.1.0";
            src = ./.;
            nativeBuildInputs = [ pkgs.makeWrapper ];
            dontBuild = true;
            installPhase = ''
              mkdir -p $out/bin $out/lib/rbx-exec $out/share/rbx-exec
              cp ${shim64}/lib/shim64.dll         $out/lib/rbx-exec/
              cp ${loader}/bin/rbx-loader.exe     $out/lib/rbx-exec/
              cp ${luau-compile}/bin/luau-compile $out/lib/rbx-exec/
              cp -r offsets                       $out/share/rbx-exec/
              install -m 0755 scripts/rbx-exec    $out/bin/rbx-exec
              wrapProgram $out/bin/rbx-exec \
                --prefix PATH : ${pkgs.lib.makeBinPath [
                  pkgs.flatpak pkgs.coreutils pkgs.gnugrep rbx-exec-ui
                ]} \
                --set RBX_EXEC_LIB  $out/lib/rbx-exec \
                --set RBX_EXEC_DATA $out/share/rbx-exec \
                --set RBX_EXEC_UI   ${rbx-exec-ui}/bin/rbx-exec-ui
            '';
          };
        };

        apps.default = {
          type    = "app";
          program = "${self.packages.${system}.default}/bin/rbx-exec";
        };

        devShells.default = pkgs.mkShell {
          buildInputs = [
            pkgs.rustc pkgs.cargo
            mingw.buildPackages.gcc
            pkgs.pkg-config pkgs.gtk4 pkgs.openssl
            pkgs.cmake pkgs.ninja pkgs.python3 pkgs.python3Packages.pyyaml
          ];
          shellHook = ''export RBX_EXEC_DEV=1'';
        };
      })

    // {
      nixosModules.default = { config, lib, pkgs, ... }:
        with lib; let
          cfg    = config.programs.rbx-exec;
          system = pkgs.stdenv.hostPlatform.system;
        in {
          options.programs.rbx-exec = {
            enable  = mkEnableOption "rbx-exec roblox executor";
            package = mkOption {
              type    = types.package;
              default = self.packages.${system}.default;
            };
          };
          config = mkIf cfg.enable {
            environment.systemPackages = [ cfg.package pkgs.flatpak ];
            services.flatpak.enable = true;
            xdg.portal = {
              enable       = true;
              extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
            };
          };
        };
    };
}
