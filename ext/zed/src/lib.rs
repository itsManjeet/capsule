use std::path::Path;

use zed_extension_api::{self as zed, LanguageServerId, Result};

struct LipiExtension;

impl LipiExtension {
    fn env_var(worktree: &zed::Worktree, name: &str) -> Option<String> {
        worktree
            .shell_env()
            .into_iter()
            .find_map(|(key, value)| (key == name).then_some(value))
    }

    fn workspace_lipi(worktree: &zed::Worktree) -> Option<String> {
        let path = format!("{}/lipi", worktree.root_path());
        Path::new(&path).is_file().then_some(path)
    }

    fn workspace_stdlib_path(worktree: &zed::Worktree) -> Option<String> {
        if worktree.read_text_file("lib/std.lipi").is_ok() {
            Some(format!("{}/lib", worktree.root_path()))
        } else {
            None
        }
    }

    fn lipi_path(worktree: &zed::Worktree) -> Result<String> {
        if let Some(path) = Self::env_var(worktree, "LIPI_LIPI") {
            if !path.is_empty() {
                return Ok(path);
            }
        }

        if let Some(path) = Self::workspace_lipi(worktree) {
            return Ok(path);
        }

        worktree.which("lipi").ok_or_else(|| {
            "lipi not found. Build LIPI with `make`, put `lipi` on PATH, or set LIPI_LIPI"
                .to_string()
        })
    }
}

impl zed::Extension for LipiExtension {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let mut env = Vec::new();
        if Self::env_var(worktree, "LIPI_STDLIB_PATH").is_none() {
            if let Some(path) = Self::workspace_stdlib_path(worktree) {
                env.push(("LIPI_STDLIB_PATH".to_string(), path));
            }
        }

        Ok(zed::Command {
            command: Self::lipi_path(worktree)?,
            args: vec!["-lsp".to_string()],
            env,
        })
    }
}

zed::register_extension!(LipiExtension);
