use std::path::PathBuf;

use anyhow::Result;
use linkembed::{config_path, load_or_create, App, LoadConfigResult, LogLevel, Logger};

#[tokio::main]
async fn main() -> Result<()> {
    let exe_path = std::env::current_exe()?;
    let exe_dir = exe_path
        .parent()
        .map(PathBuf::from)
        .ok_or_else(|| anyhow::anyhow!("Cannot determine executable directory"))?;

    let logger = Logger::new(&exe_dir, LogLevel::Info)?;

    let config = match load_or_create(&exe_dir)? {
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
        format!(
            "Configuration loaded from: {}",
            config_path(&exe_dir).display()
        ),
    );

    let app = App::new(exe_dir, config, logger)?;
    app.run().await
}
