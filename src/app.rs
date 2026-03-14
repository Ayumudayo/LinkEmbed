use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use anyhow::{anyhow, Result};
use reqwest::redirect::Policy;
use reqwest::Client as HttpClient;
use serenity::all::{
    ChannelId, Client, Context, CreateAllowedMentions, CreateEmbed, CreateEmbedAuthor,
    CreateMessage, GatewayIntents, GuildId, Http, Message, MessageId,
    MessageReference, MessageUpdateEvent, Ready,
};
use serenity::async_trait;
use serenity::client::EventHandler;
use tokio::sync::Semaphore;
use tokio::task::JoinHandle;
use tokio::time::sleep;

use crate::cache::MetadataCache;
use crate::config::Config;
use crate::fetch::fetch_html;
use crate::logger::{LogLevel, Logger};
use crate::metadata::{parse_metadata, Metadata};
use crate::rate_limiter::RateLimiter;
use crate::url_util::{display_host, extract_urls, proxy_image_if_needed, resolve_against};

#[derive(Clone)]
pub struct App {
    inner: Arc<AppState>,
}

struct AppState {
    exe_dir: PathBuf,
    config: Config,
    logger: Logger,
    http_client: HttpClient,
    rate_limiter: RateLimiter,
    cache: MetadataCache,
    worker_limiter: Arc<Semaphore>,
    scheduled_jobs: Mutex<HashMap<MessageId, ScheduledJob>>,
    bot_replies: Mutex<HashMap<MessageId, MessageId>>,
    next_job_token: AtomicU64,
}

struct ScheduledJob {
    token: u64,
    handle: JoinHandle<()>,
}

#[derive(Clone)]
struct Handler {
    app: App,
}

#[derive(Clone)]
struct JobContext {
    url: String,
    channel_id: ChannelId,
    message_id: MessageId,
}

impl App {
    pub fn new(exe_dir: PathBuf, config: Config, logger: Logger) -> Result<Self> {
        let hardware_cores = std::thread::available_parallelism()
            .map(|count| count.get())
            .unwrap_or(1);
        let default_workers = std::cmp::max(1, hardware_cores / 2);
        let worker_count = if config.max_concurrency == 0 {
            default_workers
        } else if config.max_concurrency < 0 || config.max_concurrency as usize > hardware_cores {
            return Err(anyhow!(
                "Configured max_concurrency ({}) is invalid. It must be between 1 and {}.",
                config.max_concurrency,
                hardware_cores
            ));
        } else {
            config.max_concurrency as usize
        };

        if config.bot_token.trim().is_empty() || config.bot_token == "YOUR_BOT_TOKEN_HERE" {
            return Err(anyhow!(
                "Please set your bot_token in {}",
                crate::config::config_path(&exe_dir).display()
            ));
        }

        let http_client = HttpClient::builder()
            .user_agent(config.http_user_agent.clone())
            .redirect(Policy::limited(config.http_max_redirects))
            .timeout(Duration::from_millis(config.http_timeout_ms))
            .brotli(true)
            .gzip(true)
            .build()?;

        logger.log(
            LogLevel::Info,
            format!("Using configured worker concurrency: {worker_count}"),
        );

        Ok(Self {
            inner: Arc::new(AppState {
                exe_dir,
                config: config.clone(),
                logger,
                http_client,
                rate_limiter: RateLimiter::new(config.rate_per_sec),
                cache: MetadataCache::new(
                    config.cache_max_size,
                    config.cache_ttl_minutes,
                    config.cache_max_bytes,
                ),
                worker_limiter: Arc::new(Semaphore::new(worker_count)),
                scheduled_jobs: Mutex::new(HashMap::new()),
                bot_replies: Mutex::new(HashMap::new()),
                next_job_token: AtomicU64::new(1),
            }),
        })
    }

    pub async fn run(self) -> Result<()> {
        let intents = GatewayIntents::GUILD_MESSAGES
            | GatewayIntents::DIRECT_MESSAGES
            | GatewayIntents::MESSAGE_CONTENT;

        let mut client = Client::builder(self.inner.config.bot_token.clone(), intents)
            .event_handler(Handler { app: self.clone() })
            .await?;

        self.inner.logger.log(
            LogLevel::Info,
            format!("Starting Rust port from {}", self.inner.exe_dir.display()),
        );
        client.start().await?;
        Ok(())
    }

    async fn on_ready(&self, ready: &Ready) {
        self.inner
            .logger
            .log(LogLevel::Info, format!("Bot is ready! Logged in as {}", ready.user.name));
    }

    async fn on_message_create(&self, ctx: &Context, message: &Message) {
        if message.author.bot || !message.embeds.is_empty() {
            return;
        }

        let Some(url) = extract_urls(&message.content).into_iter().last() else {
            return;
        };

        self.inner.logger.log(
            LogLevel::Info,
            format!("Scheduling job for URL: {url}"),
        );
        self.schedule_job(
            ctx.http.clone(),
            JobContext {
                url,
                channel_id: message.channel_id,
                message_id: message.id,
            },
        );
    }

    async fn on_message_update(
        &self,
        ctx: &Context,
        old: Option<Message>,
        new: Option<Message>,
        event: &MessageUpdateEvent,
    ) {
        if event.author.as_ref().is_some_and(|user| user.bot) {
            return;
        }

        let has_discord_embed = event.embeds.as_ref().is_some_and(|embeds| !embeds.is_empty());
        let latest_content = event
            .content
            .clone()
            .or_else(|| new.as_ref().map(|message| message.content.clone()))
            .or_else(|| old.as_ref().map(|message| message.content.clone()));
        let url_removed = latest_content
            .as_deref()
            .map(|content| extract_urls(content).is_empty())
            .unwrap_or(false);

        if has_discord_embed || url_removed {
            self.cancel_job(event.id);
            self.delete_bot_reply(ctx.http.clone(), event.channel_id, event.id)
                .await;

            let reason = if has_discord_embed {
                "Message updated with embed"
            } else {
                "Message updated without URLs"
            };
            self.inner
                .logger
                .log(LogLevel::Info, format!("{reason}, cancelling job for {}", event.id));
        }
    }

    async fn on_message_delete(
        &self,
        ctx: &Context,
        channel_id: ChannelId,
        deleted_message_id: MessageId,
        _guild_id: Option<GuildId>,
    ) {
        self.cancel_job(deleted_message_id);
        self.delete_bot_reply(ctx.http.clone(), channel_id, deleted_message_id)
            .await;
    }

    fn schedule_job(&self, http: Arc<Http>, job: JobContext) {
        let token = self.inner.next_job_token.fetch_add(1, Ordering::Relaxed);
        let delay = Duration::from_secs(self.inner.config.embed_delay_seconds);
        let app = self.clone();
        let message_id = job.message_id;

        let handle = tokio::spawn(async move {
            sleep(delay).await;
            if !app.begin_job(message_id, token) {
                return;
            }

            let Ok(_permit) = app.inner.worker_limiter.clone().acquire_owned().await else {
                return;
            };

            app.process_url(http, job).await;
        });

        if let Ok(mut scheduled_jobs) = self.inner.scheduled_jobs.lock() {
            if let Some(existing) = scheduled_jobs.insert(message_id, ScheduledJob { token, handle }) {
                existing.handle.abort();
            }
        }
    }

    fn cancel_job(&self, message_id: MessageId) {
        if let Ok(mut scheduled_jobs) = self.inner.scheduled_jobs.lock()
            && let Some(existing) = scheduled_jobs.remove(&message_id)
        {
            existing.handle.abort();
        }
    }

    fn begin_job(&self, message_id: MessageId, token: u64) -> bool {
        let Ok(mut scheduled_jobs) = self.inner.scheduled_jobs.lock() else {
            return false;
        };

        if let Some(current) = scheduled_jobs.get(&message_id)
            && current.token == token
        {
            scheduled_jobs.remove(&message_id);
            return true;
        }

        false
    }

    async fn delete_bot_reply(
        &self,
        http: Arc<Http>,
        channel_id: ChannelId,
        original_message_id: MessageId,
    ) {
        let bot_reply_id = self
            .inner
            .bot_replies
            .lock()
            .ok()
            .and_then(|mut replies| replies.remove(&original_message_id));

        if let Some(bot_reply_id) = bot_reply_id {
            if let Err(error) = channel_id.delete_message(http.as_ref(), bot_reply_id).await {
                self.inner.logger.log(
                    LogLevel::Warn,
                    format!("Failed to delete bot reply for {original_message_id}: {error}"),
                );
            } else {
                self.inner.logger.log(
                    LogLevel::Info,
                    format!("Deleted bot embed for original message {original_message_id}"),
                );
            }
        }
    }

    async fn process_url(&self, http: Arc<Http>, job: JobContext) {
        if !self.inner.rate_limiter.try_acquire() {
            self.inner.logger.log(
                LogLevel::Warn,
                format!("Rate limit exceeded. Dropping request for URL: {}", job.url),
            );
            return;
        }

        if let Some(metadata) = self.inner.cache.get(&job.url) {
            self.inner
                .logger
                .log(LogLevel::Info, format!("Cache hit for URL: {}", job.url));
            self.send_embed(http, &job, metadata).await;
            return;
        }

        self.inner
            .logger
            .log(LogLevel::Info, format!("Processing URL: {}", job.url));

        let max_html_bytes = self.inner.config.max_html_bytes.max(1);
        let mut attempt_bytes = self
            .inner
            .config
            .html_initial_range_bytes
            .min(max_html_bytes);
        if attempt_bytes == 0 {
            attempt_bytes = max_html_bytes;
        }

        loop {
            let mut result = fetch_html(&self.inner.http_client, &job.url, attempt_bytes, true).await;

            if result.content.is_empty() && result.status_code >= 400 {
                self.inner.logger.log(
                    LogLevel::Debug,
                    format!(
                        "Range fetch returned status {} with no content. Retrying without range for {}",
                        result.status_code, job.url
                    ),
                );
                result = fetch_html(&self.inner.http_client, &job.url, attempt_bytes, false).await;
            }

            if let Some(error) = result.error {
                self.inner.logger.log(
                    LogLevel::Error,
                    format!(
                        "Failed to fetch {}: status={}, err={}",
                        job.url, result.status_code, error
                    ),
                );
                return;
            }

            self.inner.logger.log(
                LogLevel::Debug,
                format!(
                    "Fetched {} bytes={} truncated={}",
                    job.url,
                    result.content.len(),
                    result.truncated
                ),
            );

            if let Some(mut metadata) = parse_metadata(&result.content) {
                if !metadata.image_url.is_empty() {
                    let base = if result.effective_url.is_empty() {
                        job.url.as_str()
                    } else {
                        result.effective_url.as_str()
                    };
                    metadata.image_url = resolve_against(base, &metadata.image_url);
                    metadata.image_url =
                        proxy_image_if_needed(&self.inner.config, &metadata.image_url);
                }

                self.inner.cache.put(job.url.clone(), metadata.clone());
                if !result.effective_url.is_empty() && result.effective_url != job.url {
                    self.inner.cache.put(result.effective_url.clone(), metadata.clone());
                }

                self.send_embed(http, &job, metadata).await;
                return;
            }

            if attempt_bytes >= max_html_bytes {
                self.inner.logger.log(
                    LogLevel::Warn,
                    format!("Could not parse metadata within max bytes from: {}", job.url),
                );
                return;
            }

            let grown = ((attempt_bytes as f64)
                * self.inner.config.html_range_growth_factor.max(1.0))
                .floor() as usize;
            attempt_bytes = grown.max(attempt_bytes + 1).min(max_html_bytes);

            self.inner.logger.log(
                LogLevel::Debug,
                format!(
                    "Metadata incomplete, increasing range to {} bytes for URL: {}",
                    attempt_bytes, job.url
                ),
            );
        }
    }

    async fn send_embed(&self, http: Arc<Http>, job: &JobContext, metadata: Metadata) {
        let Some(message) = self
            .fetch_original_message(http.as_ref(), job.channel_id, job.message_id)
            .await
        else {
            return;
        };

        if !message.embeds.is_empty() {
            self.inner.logger.log(
                LogLevel::Info,
                format!("Discord already attached an embed. Skipping bot embed for: {}", job.url),
            );
            return;
        }

        if extract_urls(&message.content).is_empty() {
            self.inner.logger.log(
                LogLevel::Info,
                format!(
                    "Original message no longer contains URLs. Skipping bot embed for: {}",
                    job.url
                ),
            );
            return;
        }

        let site_name = if metadata.site_name.is_empty() {
            display_host(&job.url).unwrap_or_default()
        } else {
            metadata.site_name.clone()
        };

        let mut embed = CreateEmbed::new();
        if !site_name.is_empty() {
            embed = embed.author(CreateEmbedAuthor::new(site_name).url(job.url.clone()));
        }
        if !metadata.title.is_empty() {
            embed = embed.title(metadata.title);
        }
        if !job.url.is_empty() {
            embed = embed.url(job.url.clone());
        }
        if !metadata.description.is_empty() {
            embed = embed.description(metadata.description);
        }
        if !metadata.image_url.is_empty() {
            embed = embed.thumbnail(metadata.image_url);
        }

        let builder = CreateMessage::new()
            .embed(embed)
            .reference_message(MessageReference::from((job.channel_id, job.message_id)))
            .allowed_mentions(CreateAllowedMentions::new().replied_user(false));

        match job.channel_id.send_message(http.as_ref(), builder).await {
            Ok(reply) => {
                if let Ok(mut replies) = self.inner.bot_replies.lock() {
                    replies.insert(job.message_id, reply.id);
                }
                self.schedule_post_send_verification(
                    http.clone(),
                    job.channel_id,
                    job.message_id,
                    reply.id,
                );
                self.inner.logger.log(
                    LogLevel::Info,
                    format!("Replied successfully to message with embed for: {}", job.url),
                );
            }
            Err(error) => {
                self.inner.logger.log(
                    LogLevel::Error,
                    format!("message_create failed for {}: {}", job.url, error),
                );
            }
        }
    }

    fn schedule_post_send_verification(
        &self,
        http: Arc<Http>,
        channel_id: ChannelId,
        original_message_id: MessageId,
        bot_reply_id: MessageId,
    ) {
        let app = self.clone();
        tokio::spawn(async move {
            for delay_secs in [2_u64, 5, 10] {
                sleep(Duration::from_secs(delay_secs)).await;

                let Some(original_message) = app
                    .fetch_original_message(http.as_ref(), channel_id, original_message_id)
                    .await
                else {
                    return;
                };

                if !original_message.embeds.is_empty() {
                    app.delete_bot_reply_if_matches(
                        http.clone(),
                        channel_id,
                        original_message_id,
                        bot_reply_id,
                    )
                    .await;
                    return;
                }
            }
        });
    }

    async fn delete_bot_reply_if_matches(
        &self,
        http: Arc<Http>,
        channel_id: ChannelId,
        original_message_id: MessageId,
        expected_reply_id: MessageId,
    ) {
        let should_delete = self
            .inner
            .bot_replies
            .lock()
            .ok()
            .and_then(|mut replies| match replies.get(&original_message_id).copied() {
                Some(current_id) if current_id == expected_reply_id => replies.remove(&original_message_id),
                _ => None,
            });

        if let Some(reply_id) = should_delete
            && let Err(error) = channel_id.delete_message(http.as_ref(), reply_id).await
        {
            self.inner.logger.log(
                LogLevel::Warn,
                format!("Failed to delete superseded bot reply for {original_message_id}: {error}"),
            );
        }
    }

    async fn fetch_original_message(
        &self,
        http: &Http,
        channel_id: ChannelId,
        message_id: MessageId,
    ) -> Option<Message> {
        match channel_id.message(http, message_id).await {
            Ok(message) => Some(message),
            Err(error) => {
                self.inner.logger.log(
                    LogLevel::Warn,
                    format!("Failed to fetch original message {message_id}: {error}"),
                );
                None
            }
        }
    }
}

#[async_trait]
impl EventHandler for Handler {
    async fn ready(&self, _ctx: Context, ready: Ready) {
        self.app.on_ready(&ready).await;
    }

    async fn message(&self, ctx: Context, message: Message) {
        self.app.on_message_create(&ctx, &message).await;
    }

    async fn message_update(
        &self,
        ctx: Context,
        old_if_available: Option<Message>,
        new: Option<Message>,
        event: MessageUpdateEvent,
    ) {
        self.app
            .on_message_update(&ctx, old_if_available, new, &event)
            .await;
    }

    async fn message_delete(
        &self,
        ctx: Context,
        channel_id: ChannelId,
        deleted_message_id: MessageId,
        guild_id: Option<GuildId>,
    ) {
        self.app
            .on_message_delete(&ctx, channel_id, deleted_message_id, guild_id)
            .await;
    }
}
