use reqwest::header::RANGE;
use reqwest::Client;

pub struct FetchResult {
    pub content: String,
    pub status_code: u16,
    pub error: Option<String>,
    pub effective_url: String,
    pub truncated: bool,
}

pub async fn fetch_html(
    client: &Client,
    url: &str,
    attempt_max_bytes: usize,
    use_range: bool,
) -> FetchResult {
    let mut request = client.get(url);
    if use_range && attempt_max_bytes > 0 {
        request = request.header(RANGE, format!("bytes=0-{}", attempt_max_bytes - 1));
    }

    let response = match request.send().await {
        Ok(response) => response,
        Err(error) => {
            return FetchResult {
                content: String::new(),
                status_code: 0,
                error: Some(error.to_string()),
                effective_url: String::new(),
                truncated: false,
            };
        }
    };

    let status_code = response.status().as_u16();
    let effective_url = response.url().to_string();
    let mut truncated = false;
    let mut content = Vec::with_capacity(attempt_max_bytes.min(64 * 1024));
    let mut response = response;

    loop {
        match response.chunk().await {
            Ok(Some(chunk)) => {
                if content.len() >= attempt_max_bytes {
                    truncated = true;
                    break;
                }

                let remaining = attempt_max_bytes.saturating_sub(content.len());
                let to_copy = chunk.len().min(remaining);
                content.extend_from_slice(&chunk[..to_copy]);

                if to_copy < chunk.len() {
                    truncated = true;
                    break;
                }
            }
            Ok(None) => break,
            Err(error) => {
                return FetchResult {
                    content: String::new(),
                    status_code,
                    error: Some(error.to_string()),
                    effective_url,
                    truncated,
                };
            }
        }
    }

    FetchResult {
        content: String::from_utf8_lossy(&content).into_owned(),
        status_code,
        error: None,
        effective_url,
        truncated,
    }
}
