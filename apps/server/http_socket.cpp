// POSIX socket loop for the demo server. Single-threaded: connections are
// handled one at a time. Each connection gets one absolute deadline for
// the whole exchange (read request, run handler, write response), so a
// client that trickles bytes slowly can hold the server for at most
// ServerOptions::requestTimeoutSeconds, not per byte.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "http.hpp"
#include "minisql/json.hpp"

namespace minisql::http {

namespace {

using Clock = std::chrono::steady_clock;

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

void configureClientSocket(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

// Waits until `fd` is ready for `events` or the deadline passes.
bool waitFor(int fd, short events, Clock::time_point deadline) {
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) return false;
        pollfd p{fd, events, 0};
        const int r = ::poll(&p, 1, static_cast<int>(left.count()));
        if (r > 0) return true;
        if (r == 0) return false;
        if (errno != EINTR) return false;
    }
}

bool sendAll(int fd, const std::string& data, Clock::time_point deadline) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
#endif
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && waitFor(fd, POLLOUT, deadline)) {
            continue;
        }
        return false;
    }
    return true;
}

// Ends the connection without discarding the response: stop sending,
// then briefly read and drop whatever the client still sends (e.g. a body
// we rejected before reading it). Closing with unread input makes some
// kernels send a TCP reset, which can destroy the response in flight.
void closeGracefully(int fd) {
    ::shutdown(fd, SHUT_WR);
    const auto deadline = Clock::now() + std::chrono::milliseconds(500);
    char buf[4096];
    std::size_t drained = 0;
    while (drained < 64 * 1024 && waitFor(fd, POLLIN, deadline)) {
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        drained += static_cast<std::size_t>(n);
    }
}

Response errorResponse(int status, const std::string& message) {
    Response r;
    r.status = status;
    r.contentType = "application/json";
    r.body = "{\"error\":{\"kind\":\"http\",\"message\":" + jsonQuote(message) + "}}";
    return r;
}

void handleConnection(int fd, const ServerOptions& options, const Handler& handler) {
    configureClientSocket(fd);
    const auto deadline = Clock::now() + std::chrono::seconds(options.requestTimeoutSeconds);
    std::string received;
    char buf[4096];
    ParseResult parsed;
    bool timedOut = false;
    while (true) {
        if (!waitFor(fd, POLLIN, deadline)) {
            timedOut = true;
            break;
        }
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) return;  // error, or client closed before a full request
        received.append(buf, static_cast<std::size_t>(n));
        parsed = parseRequest(received, options.limits);
        if (parsed.status != ParseStatus::Incomplete) break;
    }

    Response response;
    if (timedOut) {
        response = errorResponse(408, "request not received in time");
    } else if (parsed.status == ParseStatus::Invalid) {
        response = errorResponse(parsed.errorStatus, parsed.errorMessage);
    } else {
        try {
            response = handler(parsed.request);
        } catch (const std::exception& e) {
            std::cerr << "handler error: " << e.what() << "\n";
            response = errorResponse(500, "internal error");
        }
    }
    // Allow the response a short grace period even if reading used up the
    // deadline, so a 408 still reaches the client.
    sendAll(fd, serializeResponse(response),
            std::max(deadline, Clock::now() + std::chrono::seconds(1)));
    closeGracefully(fd);
    const bool ok = parsed.status == ParseStatus::Complete;
    std::cerr << (ok ? parsed.request.method : "-") << ' ' << (ok ? parsed.request.path : "-")
              << ' ' << response.status << '\n';
}

}  // namespace

int serve(const ServerOptions& options, const Handler& handler) {
    std::signal(SIGPIPE, SIG_IGN);
    struct sigaction sa {};
    sa.sa_handler = onStopSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
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
        // Wake up regularly so a stop signal is never missed while idle.
        pollfd p{listener.get(), POLLIN, 0};
        const int ready = ::poll(&p, 1, 500);
        if (ready <= 0) continue;  // timeout or EINTR: re-check gStop
        const int client = ::accept(listener.get(), nullptr, nullptr);
        if (client < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != ECONNABORTED) {
                // e.g. EMFILE: back off instead of spinning on the error.
                std::cerr << "accept: " << std::strerror(errno) << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
        Fd conn(client);
        handleConnection(conn.get(), options, handler);
    }
    std::cerr << "shutting down\n";
    return 0;
}

}  // namespace minisql::http
