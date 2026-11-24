# rbx-exec

Roblox executor for Linux via the [Sober](https://sober.vinegarhq.org/) runtime, packaged for NixOS with a flake.

```
flake.nix          nix build + devShell + nixosModule
shim/              shim64.dll (mingw) — in-process luau bridge
loader/            rbx-loader.exe (mingw) — manual-map + APC
luau/              pinned luau for host-side bytecode compile
ui/                gtk4 + rust frontend (editor, console, attach/eject)
offsets/           signature db + pattern yaml
tools/update.py    offset scraper, run after each Roblox push
scripts/rbx-exec   runtime entrypoint (installed by nix wrapper)
build.sh           one-shot fallback build outside nix
```

## nixos — flake install

`/etc/nixos/configuration.nix`:

```nix
{
  imports = [ (builtins.getFlake "github:you/rbx-exec").nixosModules.default ];
  programs.rbx-exec.enable = true;
}
```

Or for a single run without system install:

```sh
nix run github:you/rbx-exec
```

## dev shell

```sh
nix develop
./build.sh
```

## first run

```sh
flatpak install -y flathub org.vinegarhq.Sober
flatpak run   org.vinegarhq.Sober      # once, to create the prefix
rbx-exec                                # launch rbx-exec-ui
```

Click **attach** — the UI launches Sober with the wrapper `SOBER_WRAP` pointed at `rbx-loader.exe`, which `CreateProcess`-suspends `RobloxPlayerBeta.exe`, manual-maps `shim64.dll` and APC-queues `DllMain` into the primary thread before Hyperion's TLS callbacks fire.

The shim opens `\\.\pipe\rbx-exec` on the Wine side; `pipe-publish.sh` symlinks the underlying wineserver socket into the prefix so the UI can speak it as a plain unix socket.

## stability notes

- offsets drift every Roblox push. Run `tools/update.py --exe <RobloxPlayerBeta.exe>` and commit the regenerated `offsets.db`.
- luau is pinned at 0.650; bump when roblox bumps.
- shim unlinks itself from `PEB_LDR_DATA` three lists on load; no cross-process ops after that.
- this is for isolated dev prefixes, not your main account.

Built by Olivia — <https://discord.gg/ojailbreaks>
