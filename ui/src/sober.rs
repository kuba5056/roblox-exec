// Host-side sober glue. Finds the sober wine prefix, copies our
// shim and loader into it, launches Roblox through sober with the
// loader wrapping the player, returns the unix path that maps to
// \\.\pipe\rbx-exec inside the prefix.

use anyhow::{anyhow, Context, Result};
use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn home() -> PathBuf {
    env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

pub fn sober_prefix() -> Result<PathBuf> {
    let h = home();
    let candidates = [
        h.join(".var/app/org.vinegarhq.Sober/data/sober/prefix"),
        h.join(".local/share/sober/prefix"),
    ];
    for c in &candidates {
        if c.exists() { return Ok(c.clone()); }
    }
    Err(anyhow!("sober prefix not found — install the Sober flatpak"))
}

pub fn install_artifacts(prefix: &Path) -> Result<(PathBuf, PathBuf, PathBuf)> {
    let lib = env::var("RBX_EXEC_LIB").unwrap_or_else(|_| ".".into());
    let dropin = prefix.join("drive_c/rbx-exec");
    std::fs::create_dir_all(&dropin).context("mkdir rbx-exec dropin")?;

    let shim_src   = PathBuf::from(&lib).join("shim64.dll");
    let loader_src = PathBuf::from(&lib).join("rbx-loader.exe");
    let shim_dst   = dropin.join("shim64.dll");
    let loader_dst = dropin.join("rbx-loader.exe");
    std::fs::copy(&shim_src,   &shim_dst  ).context("copy shim")?;
    std::fs::copy(&loader_src, &loader_dst).context("copy loader")?;
    Ok((dropin, shim_dst, loader_dst))
}

pub fn pipe_socket_path(prefix: &Path) -> PathBuf {
    // Wine exposes named pipes under /tmp/.wine-<uid>/server-<dev>-<ino>/
    // but the sober flatpak sandbox proxies them to the prefix's own
    // XDG runtime dir. We ask sober to publish the mapping at a known
    // path via a tiny helper script dropped next to the loader.
    prefix.join("drive_c/rbx-exec/rbx-exec.sock")
}

pub fn launch_and_inject() -> Result<String> {
    let prefix = sober_prefix()?;
    let (_drop, shim, loader) = install_artifacts(&prefix)?;

    let shim_z   = to_wine_z(&shim);
    let loader_z = to_wine_z(&loader);

    // Sober launches Roblox itself; we point it at our loader as a
    // "wrapper" through the SOBER_WRAP env var (sober >= 1.14 reads this).
    let status = Command::new("flatpak")
        .args(["run", "--command=sober", "org.vinegarhq.Sober"])
        .env("SOBER_WRAP",      &loader_z)
        .env("RBX_EXEC_SHIM",   &shim_z)
        .env("RBX_EXEC_ARGS",   "")
        .spawn()
        .context("spawn sober")?;
    // detach; sober keeps running
    std::mem::forget(status);

    let sock = pipe_socket_path(&prefix);
    for _ in 0..40 {
        if sock.exists() {
            return Ok(sock.to_string_lossy().into_owned());
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }
    Err(anyhow!("timed out waiting for {}", sock.display()))
}

fn to_wine_z(p: &Path) -> String {
    let s = p.to_string_lossy().replace('/', "\\");
    format!("Z:{s}")
}
