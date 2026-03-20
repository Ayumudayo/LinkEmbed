use std::sync::OnceLock;

use encoding_rs::{Encoding, EUC_KR, UTF_8};
use regex::Regex;
use reqwest::header::{CONTENT_TYPE, RANGE};
use reqwest::Client;

pub struct FetchResult {
    pub content: String,
    pub status_code: u16,
    pub error: Option<String>,
    pub effective_url: String,
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
            };
        }
    };

    let status_code = response.status().as_u16();
    let effective_url = response.url().to_string();
    let content_type = response
        .headers()
        .get(CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .map(str::to_owned);
    let mut content = Vec::with_capacity(attempt_max_bytes.min(64 * 1024));
    let mut response = response;

    loop {
        match response.chunk().await {
            Ok(Some(chunk)) => {
                if content.len() >= attempt_max_bytes {
                    break;
                }

                let remaining = attempt_max_bytes.saturating_sub(content.len());
                let to_copy = chunk.len().min(remaining);
                content.extend_from_slice(&chunk[..to_copy]);

                if to_copy < chunk.len() {
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
                };
            }
        }
    }

    FetchResult {
        content: decode_html(&content, content_type.as_deref()),
        status_code,
        error: None,
        effective_url,
    }
}

fn decode_html(content: &[u8], content_type: Option<&str>) -> String {
    let encoding = detect_encoding(content, content_type);
    let (decoded, _, _) = encoding.decode(content);
    decoded.into_owned()
}

fn detect_encoding(content: &[u8], content_type: Option<&str>) -> &'static Encoding {
    if let Some((encoding, _)) = Encoding::for_bom(content) {
        return encoding;
    }

    if let Some(encoding) = content_type
        .and_then(parse_charset_from_content_type)
        .and_then(|label| lookup_encoding(&label))
    {
        return encoding;
    }

    if let Some(encoding) =
        sniff_charset_from_html(content).and_then(|label| lookup_encoding(&label))
    {
        return encoding;
    }

    UTF_8
}

fn parse_charset_from_content_type(value: &str) -> Option<String> {
    value
        .split(';')
        .skip(1)
        .find_map(|part| {
            let (name, value) = part.trim().split_once('=')?;
            name.trim()
                .eq_ignore_ascii_case("charset")
                .then(|| value.trim().trim_matches(['"', '\'']).to_string())
        })
        .filter(|value| !value.is_empty())
}

fn sniff_charset_from_html(content: &[u8]) -> Option<String> {
    let snippet = String::from_utf8_lossy(&content[..content.len().min(4 * 1024)]);

    meta_charset_regex()
        .captures(&snippet)
        .or_else(|| meta_content_type_charset_regex().captures(&snippet))
        .and_then(|captures| captures.get(1).map(|value| value.as_str().to_string()))
}

fn meta_charset_regex() -> &'static Regex {
    static REGEX: OnceLock<Regex> = OnceLock::new();
    REGEX.get_or_init(|| {
        Regex::new(r#"(?is)<meta[^>]+charset\s*=\s*["']?\s*([a-zA-Z0-9._-]+)"#).unwrap()
    })
}

fn meta_content_type_charset_regex() -> &'static Regex {
    static REGEX: OnceLock<Regex> = OnceLock::new();
    REGEX.get_or_init(|| {
        Regex::new(
            r#"(?is)<meta[^>]+content\s*=\s*["'][^"']*charset\s*=\s*([a-zA-Z0-9._-]+)[^"']*["']"#,
        )
        .unwrap()
    })
}

fn lookup_encoding(label: &str) -> Option<&'static Encoding> {
    let normalized = label.trim();
    Encoding::for_label(normalized.as_bytes()).or_else(|| {
        match normalized.to_ascii_lowercase().as_str() {
            "ms949" | "cp949" | "windows-949" | "ksc5601" | "ks_c_5601-1987" => Some(EUC_KR),
            _ => None,
        }
    })
}

#[cfg(test)]
mod tests {
    use encoding_rs::EUC_KR;
    use reqwest::Client;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpListener;
    use tokio::task::JoinHandle;

    use super::fetch_html;

    struct TestServer {
        base_url: String,
        handle: JoinHandle<()>,
    }

    impl Drop for TestServer {
        fn drop(&mut self) {
            self.handle.abort();
        }
    }

    struct Response {
        headers: Vec<(String, String)>,
        body: Vec<u8>,
    }

    async fn spawn_server(response: Response) -> TestServer {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        let handle = tokio::spawn(async move {
            loop {
                let Ok((mut socket, _)) = listener.accept().await else {
                    break;
                };
                let headers = response.headers.clone();
                let body = response.body.clone();
                tokio::spawn(async move {
                    let mut buf = [0_u8; 4096];
                    loop {
                        let Ok(read) = socket.read(&mut buf).await else {
                            return;
                        };
                        if read == 0 || buf[..read].windows(4).any(|window| window == b"\r\n\r\n") {
                            break;
                        }
                    }

                    let mut payload = format!(
                        "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n",
                        body.len()
                    );
                    for (name, value) in headers {
                        payload.push_str(&format!("{name}: {value}\r\n"));
                    }
                    payload.push_str("\r\n");

                    let _ = socket.write_all(payload.as_bytes()).await;
                    let _ = socket.write_all(&body).await;
                });
            }
        });

        TestServer {
            base_url: format!("http://{addr}"),
            handle,
        }
    }

    #[tokio::test]
    async fn decodes_ms949_using_content_type_charset() {
        let html = r#"
            <html>
              <head>
                <title>전기차동호회-아이오닉5 6 9</title>
              </head>
            </html>
        "#;
        let (body, _, _) = EUC_KR.encode(html);
        let server = spawn_server(Response {
            headers: vec![(
                "Content-Type".to_string(),
                "text/html; charset=MS949".to_string(),
            )],
            body: body.into_owned(),
        })
        .await;

        let result = fetch_html(&Client::new(), &server.base_url, 4096, false).await;

        assert!(result.error.is_none());
        assert!(result.content.contains("전기차동호회-아이오닉5 6 9"));
    }

    #[tokio::test]
    async fn sniffs_meta_charset_when_header_omits_it() {
        let html = r#"
            <html>
              <head>
                <meta http-equiv="Content-Type" content="text/html; charset=euc-kr">
                <title>네이버 카페 테스트</title>
              </head>
            </html>
        "#;
        let (body, _, _) = EUC_KR.encode(html);
        let server = spawn_server(Response {
            headers: vec![("Content-Type".to_string(), "text/html".to_string())],
            body: body.into_owned(),
        })
        .await;

        let result = fetch_html(&Client::new(), &server.base_url, 4096, false).await;

        assert!(result.error.is_none());
        assert!(result.content.contains("네이버 카페 테스트"));
    }
}
