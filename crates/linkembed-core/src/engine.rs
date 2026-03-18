use std::sync::Arc;
use std::time::Duration;

use anyhow::Result;
use reqwest::redirect::Policy;
use reqwest::Client;

use crate::cache::MetadataCache;
use crate::fetch::fetch_html;
use crate::metadata::{parse_metadata, Metadata};
use crate::rate_limiter::RateLimiter;
use crate::url_util::{proxy_image_if_needed, resolve_against};

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ImageProxyConfig {
    pub enabled: bool,
    pub base: String,
    pub query: String,
    pub hosts: Vec<String>,
}

#[derive(Clone, Debug)]
pub struct PreviewEngineConfig {
    pub cache_ttl_minutes: u64,
    pub cache_max_size: usize,
    pub cache_max_bytes: usize,
    pub http_timeout_ms: u64,
    pub http_max_redirects: usize,
    pub http_user_agent: String,
    pub rate_per_sec: f64,
    pub max_html_bytes: usize,
    pub html_initial_range_bytes: usize,
    pub html_range_growth_factor: f64,
    pub image_proxy: ImageProxyConfig,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Preview {
    pub title: String,
    pub image_url: String,
    pub description: String,
    pub site_name: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PreviewSkipReason {
    RateLimited,
    NoMetadata,
    FetchFailed { status_code: u16, message: String },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PreviewOutcome {
    Ready(Preview),
    Skipped(PreviewSkipReason),
}

#[derive(Clone)]
pub struct PreviewEngine {
    inner: Arc<PreviewEngineState>,
}

struct PreviewEngineState {
    config: PreviewEngineConfig,
    client: Client,
    rate_limiter: RateLimiter,
    cache: MetadataCache,
}

impl PreviewEngine {
    pub fn new(config: PreviewEngineConfig) -> Result<Self> {
        let client = Client::builder()
            .user_agent(config.http_user_agent.clone())
            .redirect(Policy::limited(config.http_max_redirects))
            .timeout(Duration::from_millis(config.http_timeout_ms))
            .brotli(true)
            .gzip(true)
            .build()?;

        Ok(Self {
            inner: Arc::new(PreviewEngineState {
                client,
                rate_limiter: RateLimiter::new(config.rate_per_sec),
                cache: MetadataCache::new(
                    config.cache_max_size,
                    config.cache_ttl_minutes,
                    config.cache_max_bytes,
                ),
                config,
            }),
        })
    }

    pub async fn resolve(&self, url: &str) -> PreviewOutcome {
        if !self.inner.rate_limiter.try_acquire() {
            return PreviewOutcome::Skipped(PreviewSkipReason::RateLimited);
        }

        if let Some(metadata) = self.inner.cache.get(url) {
            return PreviewOutcome::Ready(metadata.into());
        }

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
            let mut result = fetch_html(&self.inner.client, url, attempt_bytes, true).await;

            if result.content.is_empty() && result.status_code >= 400 {
                result = fetch_html(&self.inner.client, url, attempt_bytes, false).await;
            }

            if let Some(message) = result.error {
                return PreviewOutcome::Skipped(PreviewSkipReason::FetchFailed {
                    status_code: result.status_code,
                    message,
                });
            }

            if let Some(mut metadata) = parse_metadata(&result.content) {
                if !metadata.image_url.is_empty() {
                    let base = if result.effective_url.is_empty() {
                        url
                    } else {
                        result.effective_url.as_str()
                    };
                    metadata.image_url = resolve_against(base, &metadata.image_url);
                    metadata.image_url =
                        proxy_image_if_needed(&self.inner.config.image_proxy, &metadata.image_url);
                }

                let preview: Preview = metadata.clone().into();
                self.inner.cache.put(url.to_string(), metadata.clone());
                if !result.effective_url.is_empty() && result.effective_url != url {
                    self.inner.cache.put(result.effective_url, metadata);
                }

                return PreviewOutcome::Ready(preview);
            }

            if attempt_bytes >= max_html_bytes {
                return PreviewOutcome::Skipped(PreviewSkipReason::NoMetadata);
            }

            let grown = ((attempt_bytes as f64)
                * self.inner.config.html_range_growth_factor.max(1.0))
                .floor() as usize;
            attempt_bytes = grown.max(attempt_bytes + 1).min(max_html_bytes);
        }
    }
}

impl From<Metadata> for Preview {
    fn from(value: Metadata) -> Self {
        Self {
            title: value.title,
            image_url: value.image_url,
            description: value.description,
            site_name: value.site_name,
        }
    }
}

#[cfg(test)]
mod tests {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::{Arc, Mutex};

    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpListener;
    use tokio::task::JoinHandle;

    use super::{
        ImageProxyConfig, PreviewEngine, PreviewEngineConfig, PreviewOutcome, PreviewSkipReason,
    };

    struct TestServer {
        base_url: String,
        handle: JoinHandle<()>,
    }

    impl Drop for TestServer {
        fn drop(&mut self) {
            self.handle.abort();
        }
    }

    #[derive(Clone)]
    struct Request {
        path: String,
        headers: Vec<(String, String)>,
    }

    struct Response {
        status: &'static str,
        headers: Vec<(String, String)>,
        body: String,
    }

    async fn spawn_server<F>(handler: F) -> TestServer
    where
        F: Fn(Request) -> Response + Send + Sync + 'static,
    {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        let handler = Arc::new(handler);
        let handle = tokio::spawn(async move {
            loop {
                let Ok((mut socket, _)) = listener.accept().await else {
                    break;
                };
                let handler = handler.clone();
                tokio::spawn(async move {
                    let mut buf = [0_u8; 4096];
                    let mut request_bytes = Vec::new();

                    loop {
                        let Ok(read) = socket.read(&mut buf).await else {
                            return;
                        };
                        if read == 0 {
                            return;
                        }
                        request_bytes.extend_from_slice(&buf[..read]);
                        if request_bytes.windows(4).any(|window| window == b"\r\n\r\n") {
                            break;
                        }
                    }

                    let request_text = String::from_utf8_lossy(&request_bytes);
                    let mut lines = request_text.split("\r\n");
                    let request_line = lines.next().unwrap_or_default();
                    let path = request_line
                        .split_whitespace()
                        .nth(1)
                        .unwrap_or("/")
                        .to_string();
                    let headers = lines
                        .take_while(|line| !line.is_empty())
                        .filter_map(|line| line.split_once(':'))
                        .map(|(name, value)| {
                            (name.trim().to_ascii_lowercase(), value.trim().to_string())
                        })
                        .collect();

                    let response = handler(Request { path, headers });
                    let mut payload = format!(
                        "HTTP/1.1 {}\r\nContent-Length: {}\r\nConnection: close\r\n",
                        response.status,
                        response.body.len()
                    );
                    for (name, value) in response.headers {
                        payload.push_str(&format!("{name}: {value}\r\n"));
                    }
                    payload.push_str("\r\n");

                    let _ = socket.write_all(payload.as_bytes()).await;
                    let _ = socket.write_all(response.body.as_bytes()).await;
                });
            }
        });

        TestServer {
            base_url: format!("http://{addr}"),
            handle,
        }
    }

    fn test_config() -> PreviewEngineConfig {
        PreviewEngineConfig {
            cache_ttl_minutes: 10,
            cache_max_size: 100,
            cache_max_bytes: 1024 * 1024,
            http_timeout_ms: 2_000,
            http_max_redirects: 5,
            http_user_agent: "LinkEmbedBot-Test/1.0".to_string(),
            rate_per_sec: 10.0,
            max_html_bytes: 4_096,
            html_initial_range_bytes: 4_096,
            html_range_growth_factor: 2.0,
            image_proxy: ImageProxyConfig::default(),
        }
    }

    fn encode_all(value: &str) -> String {
        let mut out = String::new();
        for byte in value.bytes() {
            if byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.' | b'~') {
                out.push(byte as char);
            } else {
                out.push('%');
                out.push(char::from(b"0123456789ABCDEF"[(byte >> 4) as usize]));
                out.push(char::from(b"0123456789ABCDEF"[(byte & 0x0F) as usize]));
            }
        }
        out
    }

    #[tokio::test]
    async fn caches_both_requested_and_effective_urls() {
        let request_count = Arc::new(AtomicUsize::new(0));
        let request_count_clone = request_count.clone();
        let server = spawn_server(move |request| {
            request_count_clone.fetch_add(1, Ordering::SeqCst);
            match request.path.as_str() {
                "/start" => Response {
                    status: "302 Found",
                    headers: vec![("Location".to_string(), "/final".to_string())],
                    body: String::new(),
                },
                "/final" => Response {
                    status: "200 OK",
                    headers: vec![("Content-Type".to_string(), "text/html".to_string())],
                    body: r#"
                        <html>
                          <head>
                            <meta property="og:title" content="Redirected" />
                          </head>
                        </html>
                    "#
                    .to_string(),
                },
                _ => Response {
                    status: "404 Not Found",
                    headers: vec![],
                    body: String::new(),
                },
            }
        })
        .await;

        let engine = PreviewEngine::new(test_config()).unwrap();
        let start_url = format!("{}/start", server.base_url);
        let final_url = format!("{}/final", server.base_url);

        assert!(matches!(
            engine.resolve(&start_url).await,
            PreviewOutcome::Ready(_)
        ));
        assert!(matches!(
            engine.resolve(&final_url).await,
            PreviewOutcome::Ready(_)
        ));
        assert_eq!(request_count.load(Ordering::SeqCst), 2);
    }

    #[tokio::test]
    async fn retries_without_range_after_failed_range_response() {
        let seen_ranges = Arc::new(Mutex::new(Vec::new()));
        let seen_ranges_clone = seen_ranges.clone();
        let server = spawn_server(move |request| {
            let range = request
                .headers
                .iter()
                .find(|(name, _)| name == "range")
                .map(|(_, value)| value.clone());
            seen_ranges_clone.lock().unwrap().push(range.clone());

            if range.is_some() {
                Response {
                    status: "416 Range Not Satisfiable",
                    headers: vec![],
                    body: String::new(),
                }
            } else {
                Response {
                    status: "200 OK",
                    headers: vec![("Content-Type".to_string(), "text/html".to_string())],
                    body: r#"
                        <html>
                          <head>
                            <meta property="og:title" content="Recovered" />
                          </head>
                        </html>
                    "#
                    .to_string(),
                }
            }
        })
        .await;

        let engine = PreviewEngine::new(test_config()).unwrap();
        let url = format!("{}/retry", server.base_url);

        assert!(matches!(engine.resolve(&url).await, PreviewOutcome::Ready(_)));

        let seen_ranges = seen_ranges.lock().unwrap();
        assert_eq!(seen_ranges.len(), 2);
        assert!(seen_ranges[0].is_some());
        assert!(seen_ranges[1].is_none());
    }

    #[tokio::test]
    async fn returns_no_metadata_when_page_has_no_preview_fields() {
        let server = spawn_server(|_| Response {
            status: "200 OK",
            headers: vec![("Content-Type".to_string(), "text/html".to_string())],
            body: "<html><body>plain body</body></html>".to_string(),
        })
        .await;

        let mut config = test_config();
        config.max_html_bytes = 128;
        config.html_initial_range_bytes = 128;
        let engine = PreviewEngine::new(config).unwrap();

        assert_eq!(
            engine.resolve(&server.base_url).await,
            PreviewOutcome::Skipped(PreviewSkipReason::NoMetadata)
        );
    }

    #[tokio::test]
    async fn returns_rate_limited_without_hitting_network() {
        let request_count = Arc::new(AtomicUsize::new(0));
        let request_count_clone = request_count.clone();
        let server = spawn_server(move |_| {
            request_count_clone.fetch_add(1, Ordering::SeqCst);
            Response {
                status: "200 OK",
                headers: vec![("Content-Type".to_string(), "text/html".to_string())],
                body: r#"
                    <html>
                      <head>
                        <meta property="og:title" content="Allowed" />
                      </head>
                    </html>
                "#
                .to_string(),
            }
        })
        .await;

        let mut config = test_config();
        config.rate_per_sec = 1.0;
        let engine = PreviewEngine::new(config).unwrap();

        let first = format!("{}/first", server.base_url);
        let second = format!("{}/second", server.base_url);
        assert!(matches!(engine.resolve(&first).await, PreviewOutcome::Ready(_)));
        assert_eq!(
            engine.resolve(&second).await,
            PreviewOutcome::Skipped(PreviewSkipReason::RateLimited)
        );
        assert_eq!(request_count.load(Ordering::SeqCst), 1);
    }

    #[tokio::test]
    async fn resolves_relative_images_and_applies_proxy() {
        let server = spawn_server(|request| Response {
            status: "200 OK",
            headers: vec![("Content-Type".to_string(), "text/html".to_string())],
            body: format!(
                r#"
                    <html>
                      <head>
                        <meta property="og:title" content="Image" />
                        <meta property="og:image" content="/viewimage.php?id=1" />
                        <meta property="og:site_name" content="Site" />
                      </head>
                    </html>
                    {}
                "#,
                request.path
            ),
        })
        .await;

        let mut config = test_config();
        config.image_proxy = ImageProxyConfig {
            enabled: true,
            base: "https://images.example/proxy".to_string(),
            query: "w=1200".to_string(),
            hosts: vec![],
        };
        let engine = PreviewEngine::new(config).unwrap();
        let page_url = format!("{}/path/page.html", server.base_url);

        let PreviewOutcome::Ready(preview) = engine.resolve(&page_url).await else {
            panic!("expected preview");
        };

        let expected_image = format!("{}/viewimage.php?id=1", server.base_url);
        let expected_proxy = format!(
            "https://images.example/proxy?url={}&w=1200",
            encode_all(&expected_image)
        );
        assert_eq!(preview.image_url, expected_proxy);
    }
}
