// Temporary smoke test for the ephsrv build environment (will be replaced).
//
// One binary, two parts:
//  - a uWS WebSocket echo server on port 18481 (vendored uWebSockets v20.80.0
//    + uSockets, built with OpenSSL), which at startup prints the Swiss
//    Ephemeris version obtained from the thread-safe fork in /shares/swisseph;
//  - a std::thread-based raw-socket WebSocket client that performs a real
//    handshake, sends masked text frame "ping" and verifies the echoed frame.
//
// uWS v20.80.0 has no client implementation (ClientApp is a placeholder),
// so the client side is a minimal hand-rolled frame writer/reader used
// purely for this smoke test.
//
// Compile (from /shares/Astrolog):
//   g++ -std=gnu++17 -O2 -I ephsrv/uWebSockets/src -I ephsrv/uSockets/src \
//       ephsrv/smoketest.cpp ephsrv/uSockets/uSockets.a \
//       -L/shares/swisseph -lswe -lssl -lcrypto -lz -lpthread -o ephsrv/smoketest

#include <App.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

#include "swephexp.h"   // Swiss Ephemeris thread-safe fork public API

static const int PORT = 18481;

static int read_full(int fd, char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static void client_thread() {
    int fd = -1;
    for (int attempt = 0; attempt < 50; attempt++) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (sockaddr *)&addr, sizeof(addr)) == 0) break;
        close(fd);
        fd = -1;
        usleep(100000);
    }
    if (fd < 0) {
        fflush(stdout); fprintf(stderr, "[client] could not connect to 127.0.0.1:%d\n", PORT);
        _exit(1);
    }
    {
        // Handshake
        const char hs[] =
            "GET / HTTP/1.1\r\n"
            "Host: 127.0.0.1:18481\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";
        std::string req(hs);
        {
            int sock = fd;
            if (::send(sock, req.data(), req.size(), 0) < 0) {
                fflush(stdout); fprintf(stderr, "[client] handshake send failed\n");
                _exit(1);
            }
            char resp[2048];
            std::string acc;
            while (acc.find("\r\n\r\n") == std::string::npos) {
                char tmp[512];
                ssize_t r = ::recv(sock, tmp, sizeof(tmp), 0);
                if (r <= 0) { fflush(stdout); fprintf(stderr, "[client] handshake read failed\n"); _exit(1); }
                acc.append(tmp, (size_t)r);
            }
            if (acc.find(" 101 ") == std::string::npos) {
                fflush(stdout); fprintf(stderr, "[client] handshake not upgraded: %.40s\n", acc.c_str());
                _exit(1);
            }
            printf("[client] handshake ok (101)\n");

            // Send masked text frame "ping" (client frames MUST be masked)
            const char *msg = "ping";
            unsigned char frame[16];
            frame[0] = 0x81;               // FIN + text
            frame[1] = 0x80 | 4;           // MASK + len 4
            unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
            memcpy(frame + 2, mask, 4);
            for (int i = 0; i < 4; i++) frame[6 + i] = (unsigned char)msg[i] ^ mask[i];
            if (::send(sock, frame, 10, 0) != 10) {
                fflush(stdout); fprintf(stderr, "[client] frame send failed\n");
                _exit(1);
            }

            // Read echo frame
            unsigned char hdr[2];
            if (read_full(sock, (char *)hdr, 2) != 0) {
                fflush(stdout); fprintf(stderr, "[client] echo header read failed\n");
                _exit(1);
            }
            size_t len = hdr[1] & 0x7F;
            std::string echo(len, '\0');
            if (len && read_full(sock, echo.data(), len) != 0) {
                fflush(stdout); fprintf(stderr, "[client] echo payload read failed\n");
                _exit(1);
            }
            if (hdr[0] == 0x81 && echo == "ping") {
                printf("[client] echo verified: sent \"ping\", got \"%s\"\n", echo.c_str());
                printf("SMOKETEST PASS\n");
                close(sock);
                fflush(stdout); _exit(0);
            }
            fflush(stdout); fprintf(stderr, "[client] unexpected echo: op=0x%02x payload=\"%s\"\n",
                    hdr[0], echo.c_str());
            _exit(1);
        }
    }
}

int main() {
    char swedesc[256];
    swe_version(swedesc);
    printf("Swiss Ephemeris version: %s\n", swedesc);

    std::thread client(client_thread);

    uWS::App().ws<char>("/*", {
        .open = [](uWS::WebSocket<false, true, char> *) {
            printf("[server] client connected\n");
        },
        .message = [](uWS::WebSocket<false, true, char> *ws, std::string_view message, uWS::OpCode) {
            ws->send(message, uWS::OpCode::TEXT);
        }
    }).listen(PORT, [](auto *token) {
        if (token) { printf("[server] listening on 18481\n"); }
        else { fprintf(stderr, "[server] listen failed\n"); _exit(1); }
    }).run();

    client.join();
    return 0;
}
