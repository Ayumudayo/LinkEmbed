#pragma once
#include <string>
#include <optional>

namespace LinkEmbed {
    class HTMLFetcher {
    public:
        struct FetchResult {
            std::string content;
            long status_code;
            std::string error;
            std::string effective_url;
            bool truncated = false;
        };

        // 기본 전체 요청(범위 사용 안 함). 최대 바이트는 설정의 max_html_bytes를 사용.
        static FetchResult Fetch(const std::string& url);

        // 범위 요청/부분 수신을 제어할 수 있는 변형.
        // attempt_max_bytes: 이 시도에서 버퍼에 저장할 최대 바이트(버퍼 상한).
        // use_range: true이면 HTTP Range 헤더로 bytes=0-(attempt_max_bytes-1) 요청 시도.
        static FetchResult Fetch(const std::string& url, size_t attempt_max_bytes, bool use_range);
    };
}