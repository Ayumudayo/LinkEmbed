use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use linkembed_core::{ImageProxyConfig, PreviewEngineConfig};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

const LEGACY_USER_AGENT: &str = "LinkEmbedBot/1.0";
const DEFAULT_USER_AGENT: &str =
    "LinkEmbedBot/1.0 (+https://github.com/Ayumudayo/LinkEmbed; Discord link preview bot)";

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    pub embed_delay_seconds: u64,
    pub cache_ttl_minutes: u64,
    pub cache_max_size: usize,
    pub cache_max_bytes: usize,
    pub http_timeout_ms: u64,
    pub http_max_redirects: usize,
    pub http_user_agent: String,
    pub max_concurrency: i32,
    pub rate_per_sec: f64,
    pub max_html_bytes: usize,
    pub html_initial_range_bytes: usize,
    pub html_range_growth_factor: f64,
    pub bot_token: String,
    pub log_level: String,
    pub image_proxy_enabled: bool,
    pub image_proxy_base: String,
    pub image_proxy_query: String,
    pub image_proxy_hosts: Vec<String>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            embed_delay_seconds: 5,
            cache_ttl_minutes: 10,
            cache_max_size: 1000,
            cache_max_bytes: 33_554_432,
            http_timeout_ms: 4000,
            http_max_redirects: 5,
            http_user_agent: DEFAULT_USER_AGENT.to_string(),
            max_concurrency: 0,
            rate_per_sec: 2.0,
            max_html_bytes: 8_388_608,
            html_initial_range_bytes: 524_288,
            html_range_growth_factor: 2.0,
            bot_token: "YOUR_BOT_TOKEN_HERE".to_string(),
            log_level: "info".to_string(),
            image_proxy_enabled: true,
            image_proxy_base: "https://images.weserv.nl".to_string(),
            image_proxy_query: "w=1200&h=630&fit=inside".to_string(),
            image_proxy_hosts: vec!["dcinside.co.kr".to_string()],
        }
    }
}

impl Config {
    pub fn preview_engine_config(&self) -> PreviewEngineConfig {
        PreviewEngineConfig {
            cache_ttl_minutes: self.cache_ttl_minutes,
            cache_max_size: self.cache_max_size,
            cache_max_bytes: self.cache_max_bytes,
            http_timeout_ms: self.http_timeout_ms,
            http_max_redirects: self.http_max_redirects,
            http_user_agent: self.http_user_agent.clone(),
            rate_per_sec: self.rate_per_sec,
            max_html_bytes: self.max_html_bytes,
            html_initial_range_bytes: self.html_initial_range_bytes,
            html_range_growth_factor: self.html_range_growth_factor,
            image_proxy: ImageProxyConfig {
                enabled: self.image_proxy_enabled,
                base: self.image_proxy_base.clone(),
                query: self.image_proxy_query.clone(),
                hosts: self.image_proxy_hosts.clone(),
            },
        }
    }
}

pub enum LoadConfigResult {
    CreatedDefault(PathBuf),
    Loaded(Config),
}

pub fn config_path(exe_dir: &Path) -> PathBuf {
    exe_dir.join("config").join("config.json")
}

pub fn load_or_create(exe_dir: &Path) -> Result<LoadConfigResult> {
    let path = config_path(exe_dir);
    if !path.exists() {
        write_default_config(&path)?;
        return Ok(LoadConfigResult::CreatedDefault(path));
    }

    let raw = fs::read_to_string(&path)
        .with_context(|| format!("Could not open config file: {}", path.display()))?;
    let mut json_value: Value =
        serde_json::from_str(&raw).with_context(|| format!("Invalid JSON in {}", path.display()))?;

    let default_value = serde_json::to_value(Config::default())?;
    let changed = merge_missing(&mut json_value, &default_value);
    let migrated = migrate_legacy_defaults(&mut json_value);
    let config: Config = serde_json::from_value(json_value.clone())?;

    if changed || migrated {
        let backup = path.with_extension("json.bak");
        let _ = fs::copy(&path, backup);
        fs::write(&path, serde_json::to_string_pretty(&json_value)?)
            .with_context(|| format!("Failed to update {}", path.display()))?;
    }

    Ok(LoadConfigResult::Loaded(config))
}

fn migrate_legacy_defaults(target: &mut Value) -> bool {
    let Some(object) = target.as_object_mut() else {
        return false;
    };

    if object
        .get("http_user_agent")
        .and_then(Value::as_str)
        .is_some_and(|value| value == LEGACY_USER_AGENT)
    {
        object.insert(
            "http_user_agent".to_string(),
            Value::String(DEFAULT_USER_AGENT.to_string()),
        );
        return true;
    }

    false
}

fn write_default_config(path: &Path) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }

    let json = serde_json::to_string_pretty(&Config::default())?;
    fs::write(path, json).with_context(|| format!("Could not write {}", path.display()))?;
    Ok(())
}

fn merge_missing(target: &mut Value, defaults: &Value) -> bool {
    match (target, defaults) {
        (Value::Object(target_map), Value::Object(default_map)) => {
            merge_object_missing(target_map, default_map)
        }
        _ => false,
    }
}

fn merge_object_missing(target: &mut Map<String, Value>, defaults: &Map<String, Value>) -> bool {
    let mut changed = false;

    for (key, default_value) in defaults {
        match target.get_mut(key) {
            Some(existing) => {
                if merge_missing(existing, default_value) {
                    changed = true;
                }
            }
            None => {
                target.insert(key.clone(), default_value.clone());
                changed = true;
            }
        }
    }

    changed
}
