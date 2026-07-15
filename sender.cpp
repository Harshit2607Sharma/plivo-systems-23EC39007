/* BASELINE SENDER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i here at t0 + i*20ms
 *                  (format: 4-byte big-endian seq + 160-byte payload)
 *   send 47001  -> relay uplink toward the receiver (YOUR wire format)
 *   bind 47004  <- feedback from your receiver, via the relay (optional)
 *
 * This baseline forwards each frame once, unchanged, and ignores feedback.
 * No redundancy, no retransmission. It cannot pass. That is the point.
 *
 * Env vars available if you want them: T0 (epoch seconds, float),
 * DURATION_S, DELAY_MS. The harness kills this process when the run ends,
 * so a forever-loop is fine.
 *
 * build: make        run: python3 run.py --delay_ms 60
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <algorithm>

#define int long long

int32_t main() {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];
    unsigned char F_prev1[160] = {0};
    unsigned char F_prev2[160] = {0};
    bool has_prev1 = false, has_prev2 = false;

    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n <= 0) continue;
        
        uint32_t seq;
        memcpy(&seq, buf, 4);
        uint32_t host_seq = ntohl(seq);

        unsigned char out_buf[324];
        memcpy(out_buf, buf, 164); 
        size_t out_len = 164;
        
        // 90% duty cycle to stay strictly under the 2.0x overhead cap
        if (has_prev1 && has_prev2 && (host_seq % 10 != 0)) { 
            unsigned char parity[160];
            for (int k = 0; k < 160; k++) {
                // Bitwise XOR of the last two payloads
                parity[k] = F_prev1[k] ^ F_prev2[k];
            }
            memcpy(out_buf + 164, parity, 160);
            out_len += 160;
        }

        sendto(out_fd, out_buf, out_len, 0, (struct sockaddr *)&relay, sizeof relay);

        // Shift history window forward
        memcpy(F_prev2, F_prev1, 160);
        memcpy(F_prev1, buf + 4, 160);
        has_prev2 = has_prev1;
        has_prev1 = true;
    }
    return 0;
}
