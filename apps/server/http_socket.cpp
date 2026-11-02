// POSIX socket loop for the demo server. Single-threaded: connections are
// handled one at a time, each bounded by the I/O timeout and size limits,
// so one slow client can delay others by at most a few seconds.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#include "http.hpp"
#include "minisql/json.hpp"

namespace minisql::http {

namespace {

volatile std::sig_atomic_t gStop = 0;

extern "C" void onStopSignal(int) { gStop = 1; }

// Owns a file descriptor (RAII).
class Fd {
public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() {
        if (fd_ >= 0) ::close(fd_);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }

private:
    int fd_;
};

void setTimeouts(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

bool sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

Response errorResponse(int status, const std::string& message) {
    Response r;
    r.status = status;
    r.contentType = "application/json";
    r.body = "{\"error\":{\"kind\":\"http\",\"message\":" + jsonQuote(message) + "}}";
    return r;
}

void handleConnection(int fd, const ServerOptions& options, const Handler& handler) {
    setTimeouts(fd, options.ioTimeoutSeconds);
    std::string received;
    char buf[4096];
    ParseResult parsed;
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n < 0 && errno == EINTR && !gStop) continue;
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                sendAll(fd, serializeResponse(errorResponse(408, "request timed out")));
            }
            return;
        }
        if (n == 0) return;  // client closed before sending a full request
        received.append(buf, static_cast<std::size_t>(n));
        parsed = parseRequest(received, options.limits);
        if (parsed.status != ParseStatus::Incomplete) break;
    }

    Response response;
    if (parsed.status == ParseStatus::Invalid) {
        response = errorResponse(parsed.errorStatus, parsed.errorMessage);
    } else {
        try {
            response = handler(parsed.request);
        } catch (const std::exception& e) {
            std::cerr << "handler error: " << e.what() << "\n";
            response = errorResponse(500, "internal error");
        }
    }
    sendAll(fd, serializeResponse(response));
    std::cerr << (parsed.status == ParseStatus::Complete ? parsed.request.method : "-") << ' '
              << (parsed.status == ParseStatus::Complete ? parsed.request.path : "-") << ' '
              << response.status << '\n';
}

}  // namespace

int serve(const ServerOptions& options, const Handler& handler) {
    std::signal(SIGPIPE, SIG_IGN);
    struct sigaction sa {};
    sa.sa_handler = onStopSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART: accept() must return EINTR on Ctrl-C
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    Fd listener(::socket(AF_INET, SOCK_STREAM, 0));
    if (listener.get() < 0) {
        std::cerr << "socket: " << std::strerror(errno) << "\n";
        return 1;
    }
    int one = 1;
    ::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(options.port));
    if (::inet_pton(AF_INET, options.host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid IPv4 address '" << options.host << "'\n";
        return 1;
    }
    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        std::cerr << "bind " << options.host << ":" << options.port << ": "
                  << std::strerror(errno) << "\n";
        return 1;
    }
    if (::listen(listener.get(), 16) != 0) {
        std::cerr << "listen: " << std::strerror(errno) << "\n";
        return 1;
    }
    std::cerr << "minisql_server listening on http://" << options.host << ":" << options.port
              << "/\n";

    while (!gStop) {
        const int client = ::accept(listener.get(), nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            std::cerr << "accept: " << std::strerror(errno) << "\n";
            continue;
        }
        Fd conn(client);
        handleConnection(conn.get(), options, handler);
    }
    std::cerr << "shutting down\n";
    return 0;
}

}  // namespace minisql::http
