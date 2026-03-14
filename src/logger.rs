use std::fmt::{Display, Formatter};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use anyhow::Result;
use time::macros::format_description;
use time::{OffsetDateTime, UtcOffset};

#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum LogLevel {
    Debug,
    Info,
    Warn,
    Error,
}

impl LogLevel {
    pub fn from_config(value: &str) -> Self {
        match value.to_ascii_lowercase().as_str() {
            "debug" => Self::Debug,
            "warn" => Self::Warn,
            "error" => Self::Error,
            _ => Self::Info,
        }
    }
}

impl Display for LogLevel {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Debug => f.write_str("DEBUG"),
            Self::Info => f.write_str("INFO"),
            Self::Warn => f.write_str("WARN"),
            Self::Error => f.write_str("ERROR"),
        }
    }
}

#[derive(Clone)]
pub struct Logger {
    inner: Arc<Mutex<LoggerState>>,
}

struct LoggerState {
    root_dir: PathBuf,
    min_level: LogLevel,
    current_date: Option<String>,
    file: Option<File>,
    local_offset: UtcOffset,
}

impl Logger {
    pub fn new(root_dir: &Path, min_level: LogLevel) -> Result<Self> {
        let logs_dir = root_dir.join("logs");
        fs::create_dir_all(&logs_dir)?;

        let local_offset = UtcOffset::current_local_offset().unwrap_or(UtcOffset::UTC);

        Ok(Self {
            inner: Arc::new(Mutex::new(LoggerState {
                root_dir: root_dir.to_path_buf(),
                min_level,
                current_date: None,
                file: None,
                local_offset,
            })),
        })
    }

    pub fn set_min_level(&self, min_level: LogLevel) {
        if let Ok(mut state) = self.inner.lock() {
            state.min_level = min_level;
        }
    }

    pub fn log(&self, level: LogLevel, message: impl AsRef<str>) {
        let message = message.as_ref();

        let mut state = match self.inner.lock() {
            Ok(guard) => guard,
            Err(_) => return,
        };

        if level < state.min_level {
            return;
        }

        let now = OffsetDateTime::now_utc().to_offset(state.local_offset);
        let timestamp_fmt = format_description!("[year]-[month]-[day] [hour]:[minute]:[second]");
        let date_fmt = format_description!("[year]-[month]-[day]");
        let timestamp = now
            .format(timestamp_fmt)
            .unwrap_or_else(|_| "0000-00-00 00:00:00".to_string());
        let date = now
            .format(date_fmt)
            .unwrap_or_else(|_| "0000-00-00".to_string());

        let line = format!("{timestamp} [{level}] {message}");
        println!("{line}");

        if state.current_date.as_deref() != Some(date.as_str()) {
            match open_log_file(&state.root_dir, &date) {
                Ok(file) => {
                    state.current_date = Some(date);
                    state.file = Some(file);
                }
                Err(_) => {
                    state.file = None;
                }
            }
        }

        if let Some(file) = state.file.as_mut() {
            let _ = writeln!(file, "{line}");
            let _ = file.flush();
        }
    }
}

fn open_log_file(root_dir: &Path, date: &str) -> io::Result<File> {
    let logs_dir = root_dir.join("logs");
    fs::create_dir_all(&logs_dir)?;

    OpenOptions::new()
        .create(true)
        .append(true)
        .open(logs_dir.join(format!("{date}.log")))
}
