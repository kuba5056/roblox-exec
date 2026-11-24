// Host-side IPC. Opens the sober-prefix pipe as a unix socket via
// winepath. Mirrors shim-side kind codes.

use anyhow::{anyhow, Context, Result};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::process::Command;
use std::time::Duration;

pub const IPC_HELLO:         u32 = 0x01;
pub const IPC_EXEC_SOURCE:   u32 = 0x10;
pub const IPC_EXEC_BYTECODE: u32 = 0x11;
pub const IPC_EJECT:         u32 = 0x20;
pub const IPC_LOG:           u32 = 0x80;
pub const IPC_RESULT:        u32 = 0x81;

pub struct Client { sock: UnixStream }

#[repr(C, packed)]
struct Msg { kind: u32, len: u32 }

impl Client {
    pub fn connect(path: &str) -> Result<Self> {
        let s = UnixStream::connect(path)
            .with_context(|| format!("connect {path}"))?;
        s.set_read_timeout(Some(Duration::from_secs(10))).ok();
        let mut c = Self { sock: s };
        c.send(IPC_HELLO, b"")?;
        let (_, msg) = c.recv()?;
        if msg.is_empty() { return Err(anyhow!("empty hello reply")); }
        Ok(c)
    }

    pub fn exec_source(&mut self, src: &str) -> Result<String> {
        // compile luau on the host, ship bytecode.
        let bc = compile_luau(src)?;
        self.send(IPC_EXEC_BYTECODE, &bc)?;
        let (code, body) = self.recv()?;
        Ok(format!("[{code}] {body}"))
    }

    pub fn eject(&mut self) -> Result<()> {
        self.send(IPC_EJECT, b"")?;
        Ok(())
    }

    fn send(&mut self, kind: u32, payload: &[u8]) -> Result<()> {
        let hdr = [kind.to_le_bytes(), (payload.len() as u32).to_le_bytes()].concat();
        self.sock.write_all(&hdr)?;
        if !payload.is_empty() { self.sock.write_all(payload)?; }
        self.sock.flush()?;
        Ok(())
    }

    fn recv(&mut self) -> Result<(i32, String)> {
        let mut hdr = [0u8; 8];
        self.sock.read_exact(&mut hdr)?;
        let _kind = u32::from_le_bytes(hdr[0..4].try_into().unwrap());
        let len   = u32::from_le_bytes(hdr[4..8].try_into().unwrap()) as usize;
        let mut buf = vec![0u8; len];
        if len > 0 { self.sock.read_exact(&mut buf)?; }
        let code = if len >= 4 {
            i32::from_le_bytes(buf[0..4].try_into().unwrap())
        } else { 0 };
        let text = String::from_utf8_lossy(&buf[4.min(len)..]).into_owned();
        Ok((code, text))
    }
}

fn compile_luau(src: &str) -> Result<Vec<u8>> {
    let lib = std::env::var("RBX_EXEC_LIB").unwrap_or_else(|_| ".".into());
    let bin = format!("{lib}/luau-compile");
    let tmp = tempfile_path("rbx-src-", ".luau");
    std::fs::write(&tmp, src)?;
    let out = Command::new(&bin)
        .arg("--binary").arg(&tmp)
        .output()
        .with_context(|| format!("run {bin}"))?;
    let _ = std::fs::remove_file(&tmp);
    if !out.status.success() {
        return Err(anyhow!("luau-compile: {}", String::from_utf8_lossy(&out.stderr)));
    }
    Ok(out.stdout)
}

fn tempfile_path(prefix: &str, suffix: &str) -> std::path::PathBuf {
    let dir = std::env::temp_dir();
    let nonce: u64 = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos() as u64;
    dir.join(format!("{prefix}{nonce}{suffix}"))
}
