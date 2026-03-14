mod app;
mod cache;
mod config;
mod fetch;
mod logger;
mod metadata;
mod rate_limiter;
mod url_util;

use std::path::PathBuf;

use anyhow::Result;
use app::App;
use config::LoadConfigResult;
use logger::{LogLevel, Logger};

#[tokio::main]
async fn main() -> Result<()> {
    let exe_path = std::env::current_exe()?;
    let exe_dir = exe_path
        .parent()
        .map(PathBuf::from)
        .ok_or_else(|| anyhow::anyhow!("Cannot determine executable directory"))?;

    let logger = Logger::new(&exe_dir, LogLevel::Info)?;

    let config = match config::load_or_create(&exe_dir)? {
        LoadConfigResult::CreatedDefault(path) => {
            logger.log(
                LogLevel::Warn,
                format!(
                    "config.json not found. Created a default config at {}. Please set your bot token and restart.",
                    path.display()
                ),
            );
            return Ok(());
        }
        LoadConfigResult::Loaded(config) => config,
    };

    logger.set_min_level(LogLevel::from_config(&config.log_level));
    logger.log(
        LogLevel::Info,
        format!("Configuration loaded from: {}", config::config_path(&exe_dir).display()),
    );

    let app = App::new(exe_dir, config, logger)?;
    app.run().await
}
