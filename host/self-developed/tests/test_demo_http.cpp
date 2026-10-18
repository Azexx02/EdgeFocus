#include "network/DemoHttpServer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        std::exit(1);
    }
}

} // namespace

int main() {
    using edgefocus::demo_http::DemoHttpParseStatus;
    using edgefocus::demo_http::DemoHttpParsedRequest;
    using edgefocus::demo_http::parseDemoHttpRequestBuffer;

    {
        const std::string body = R"({"cmd":"ping"})";
        const std::string header =
            "POST /demo/command HTTP/1.1\r\n"
            "Host: 192.168.10.210\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n";

        DemoHttpParsedRequest request;
        std::string error;
        size_t expected_bytes = 0;
        const auto partial_status = parseDemoHttpRequestBuffer(
            header + body.substr(0, body.size() - 3), request, expected_bytes, error);
        require(partial_status == DemoHttpParseStatus::INCOMPLETE,
                "split body should stay incomplete until Content-Length bytes arrive");
        require(expected_bytes == header.size() + body.size(),
                "expected byte count should match header + full body size");

        const auto full_status =
            parseDemoHttpRequestBuffer(header + body, request, expected_bytes, error);
        require(full_status == DemoHttpParseStatus::COMPLETE,
                "complete request should parse successfully");
        require(request.method == "POST", "method should parse from request line");
        require(request.path == "/demo/command", "path should parse from request line");
        require(request.body == body, "body should honor Content-Length");
    }

    {
        DemoHttpParsedRequest request;
        std::string error;
        size_t expected_bytes = 0;
        const auto status = parseDemoHttpRequestBuffer(
            "POST /demo/command HTTP/1.1\r\n"
            "Content-Length: nope\r\n\r\n",
            request, expected_bytes, error);
        require(status == DemoHttpParseStatus::ERROR,
                "invalid Content-Length should be rejected");
        require(!error.empty(), "invalid Content-Length should report an error");
    }

    {
        DemoHttpParsedRequest request;
        std::string error;
        size_t expected_bytes = 0;
        const std::string oversized_header(20000, 'a');
        const auto status =
            parseDemoHttpRequestBuffer(oversized_header, request, expected_bytes, error);
        require(status == DemoHttpParseStatus::ERROR,
                "oversized header without terminator should be rejected");
        require(error == "request header too large",
                "oversized header should report the specific boundary");
    }

    std::cout << "[PASS] test_demo_http" << std::endl;
    return 0;
}
