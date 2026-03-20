mod embed_renderer;
mod reply_tracker;
mod scheduler;

use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use anyhow::{anyhow, Result};
use linkembed_core::{extract_urls, Preview, PreviewEngine, PreviewOutcome, PreviewSkipReason};
use serenity::all::{
    ChannelId, Client, Context, GatewayIntents, GuildId, Http, Message, MessageId,
    MessageUpdateEvent, Ready,
};
use serenity::async_trait;
use serenity::client::EventHandler;
use tokio::time::sleep;

use self::embed_renderer::EmbedRenderer;
use self::reply_tracker::ReplyTracker;
use self::scheduler::JobScheduler;
use crate::config::{config_path, Config};
use crate::logger::{LogLevel, Logger};

#[derive(Clone)]
pub struct DiscordBot {
    inner: Arc<BotState>,
}

struct BotState {
    exe_dir: PathBuf,
    config: Config,
    logger: Logger,
    preview_engine: PreviewEngine,
    scheduler: JobScheduler,
    replies: ReplyTracker,
    embed_renderer: EmbedRenderer,
}

#[derive(Clone)]
struct Handler {
    bot: DiscordBot,
}

#[derive(Clone)]
struct JobContext {
    url: String,
    channel_id: ChannelId,
    message_id: MessageId,
}

impl DiscordBot {
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
                config_path(&exe_dir).display()
            ));
        }

        logger.log(
            LogLevel::Info,
            format!("Using configured worker concurrency: {worker_count}"),
        );

        Ok(Self {
            inner: Arc::new(BotState {
                exe_dir,
                preview_engine: PreviewEngine::new(config.preview_engine_config())?,
                scheduler: JobScheduler::new(
                    Duration::from_secs(config.embed_delay_seconds),
                    worker_count,
                ),
                replies: ReplyTracker::default(),
                embed_renderer: EmbedRenderer::default(),
                config,
                logger,
            }),
        })
    }

    pub async fn run(self) -> Result<()> {
        let intents = GatewayIntents::GUILD_MESSAGES
            | GatewayIntents::DIRECT_MESSAGES
            | GatewayIntents::MESSAGE_CONTENT;

        let mut client = Client::builder(self.inner.config.bot_token.clone(), intents)
            .event_handler(Handler { bot: self.clone() })
            .await?;

        self.inner.logger.log(
            LogLevel::Info,
            format!("Starting Rust port from {}", self.inner.exe_dir.display()),
        );
        client.start().await?;
        Ok(())
    }

    async fn on_ready(&self, ready: &Ready) {
        self.inner.logger.log(
            LogLevel::Info,
            format!("Bot is ready! Logged in as {}", ready.user.name),
        );
    }

    async fn on_message_create(&self, ctx: &Context, message: &Message) {
        if message.author.bot || !message.embeds.is_empty() {
            return;
        }

        let Some(url) = select_last_url(&message.content) else {
            return;
        };

        self.inner
            .logger
            .log(LogLevel::Info, format!("Scheduling job for URL: {url}"));
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

        let has_discord_embed = event
            .embeds
            .as_ref()
            .is_some_and(|embeds| !embeds.is_empty());
        let latest_content = event
            .content
            .clone()
            .or_else(|| new.as_ref().map(|message| message.content.clone()))
            .or_else(|| old.as_ref().map(|message| message.content.clone()));
        let url_removed = latest_content
            .as_deref()
            .map(|content| !contains_urls(content))
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
            self.inner.logger.log(
                LogLevel::Info,
                format!("{reason}, cancelling job for {}", event.id),
            );
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
        let bot = self.clone();
        let job_clone = job.clone();
        self.inner
            .scheduler
            .schedule(job.message_id, move || async move {
                bot.process_url(http, job_clone).await;
            });
    }

    fn cancel_job(&self, message_id: MessageId) {
        self.inner.scheduler.cancel(message_id);
    }

    async fn delete_bot_reply(
        &self,
        http: Arc<Http>,
        channel_id: ChannelId,
        original_message_id: MessageId,
    ) {
        let bot_reply_id = self.inner.replies.take_reply(original_message_id);

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
        self.inner
            .logger
            .log(LogLevel::Info, format!("Processing URL: {}", job.url));

        match self.inner.preview_engine.resolve(&job.url).await {
            PreviewOutcome::Ready(preview) => {
                self.send_embed(http, &job, preview).await;
            }
            PreviewOutcome::Skipped(PreviewSkipReason::RateLimited) => {
                self.inner.logger.log(
                    LogLevel::Warn,
                    format!("Rate limit exceeded. Dropping request for URL: {}", job.url),
                );
            }
            PreviewOutcome::Skipped(PreviewSkipReason::NoMetadata) => {
                self.inner.logger.log(
                    LogLevel::Warn,
                    format!(
                        "Could not parse metadata within max bytes from: {}",
                        job.url
                    ),
                );
            }
            PreviewOutcome::Skipped(PreviewSkipReason::FetchFailed {
                status_code,
                message,
            }) => {
                self.inner.logger.log(
                    LogLevel::Error,
                    format!(
                        "Failed to fetch {}: status={}, err={}",
                        job.url, status_code, message
                    ),
                );
            }
        }
    }

    async fn send_embed(&self, http: Arc<Http>, job: &JobContext, preview: Preview) {
        let Some(message) = self
            .fetch_original_message(http.as_ref(), job.channel_id, job.message_id)
            .await
        else {
            return;
        };

        if !message.embeds.is_empty() {
            self.inner.logger.log(
                LogLevel::Info,
                format!(
                    "Discord already attached an embed. Skipping bot embed for: {}",
                    job.url
                ),
            );
            return;
        }

        if !contains_urls(&message.content) {
            self.inner.logger.log(
                LogLevel::Info,
                format!(
                    "Original message no longer contains URLs. Skipping bot embed for: {}",
                    job.url
                ),
            );
            return;
        }

        let builder = self.inner.embed_renderer.build_reply(
            job.channel_id,
            job.message_id,
            &job.url,
            preview,
        );

        match job.channel_id.send_message(http.as_ref(), builder).await {
            Ok(reply) => {
                self.inner.replies.remember_reply(job.message_id, reply.id);
                self.schedule_post_send_verification(
                    http.clone(),
                    job.channel_id,
                    job.message_id,
                    reply.id,
                );
                self.inner.logger.log(
                    LogLevel::Info,
                    format!(
                        "Replied successfully to message with embed for: {}",
                        job.url
                    ),
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
        let bot = self.clone();
        tokio::spawn(async move {
            for delay_secs in [2_u64, 5, 10] {
                sleep(Duration::from_secs(delay_secs)).await;

                let Some(original_message) = bot
                    .fetch_original_message(http.as_ref(), channel_id, original_message_id)
                    .await
                else {
                    return;
                };

                if !original_message.embeds.is_empty() {
                    bot.delete_bot_reply_if_matches(
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
        let reply_id = self
            .inner
            .replies
            .take_reply_if_matches(original_message_id, expected_reply_id);

        if let Some(reply_id) = reply_id {
            if let Err(error) = channel_id.delete_message(http.as_ref(), reply_id).await {
                self.inner.logger.log(
                    LogLevel::Warn,
                    format!(
                        "Failed to delete superseded bot reply for {original_message_id}: {error}"
                    ),
                );
            }
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

fn select_last_url(text: &str) -> Option<String> {
    extract_urls(text).into_iter().last()
}

fn contains_urls(text: &str) -> bool {
    !extract_urls(text).is_empty()
}

#[async_trait]
impl EventHandler for Handler {
    async fn ready(&self, _ctx: Context, ready: Ready) {
        self.bot.on_ready(&ready).await;
    }

    async fn message(&self, ctx: Context, message: Message) {
        self.bot.on_message_create(&ctx, &message).await;
    }

    async fn message_update(
        &self,
        ctx: Context,
        old_if_available: Option<Message>,
        new: Option<Message>,
        event: MessageUpdateEvent,
    ) {
        self.bot
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
        self.bot
            .on_message_delete(&ctx, channel_id, deleted_message_id, guild_id)
            .await;
    }
}

#[cfg(test)]
mod tests {
    use super::select_last_url;

    #[test]
    fn selects_last_url_from_message_content() {
        assert_eq!(
            select_last_url("first https://example.com/a second https://example.com/b"),
            Some("https://example.com/b".to_string())
        );
    }
}
