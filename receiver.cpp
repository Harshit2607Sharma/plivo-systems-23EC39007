/* BASELINE RECEIVER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte big-endian seq +
 *                  160-byte payload. Frame i counts only if it arrives
 *                  BEFORE its deadline t0 + DELAY_MS + i*20ms.
 *   send 47003  -> feedback to your sender, via the relay (optional)
 *
 * This baseline forwards whatever arrives straight to the player: lost
 * frames stay lost, late frames stay late, duplicates are re-sent
 * harmlessly. All yours to fix — jitter buffer, reordering, recovery.
 *
 * Env vars available: T0, DURATION_S, DELAY_MS. Harness kills the process
 * at run end; a forever-loop is fine.
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <algorithm>

#define int long long

bool seen[200000] = {false};
unsigned char payloads[200000][160];

bool has_parity[200000] = {false};
unsigned char parities[200000][160];

// Helper to instantly construct and fire a completed packet
void fire_packet(int seq, unsigned char* payload, int out_fd, struct sockaddr_in& player) {
    if (!seen[seq]) {
        unsigned char frame[164];
        uint32_t net_seq = htonl(seq);
        memcpy(frame, &net_seq, 4);
        memcpy(frame + 4, payload, 160);
        sendto(out_fd, frame, 164, 0, (struct sockaddr *)&player, sizeof player);
        
        seen[seq] = true;
        memcpy(payloads[seq], payload, 160); // Store for future XOR deductions
    }
}

int32_t main() {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];

    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n <= 0) continue;

        uint32_t seq1;
        memcpy(&seq1, buf, 4);
        int host_seq = ntohl(seq1);

        if (n >= 164) {
            fire_packet(host_seq, buf + 4, out_fd, player);
        }
        
        if (n >= 324) {
            has_parity[host_seq] = true;
            memcpy(parities[host_seq], buf + 164, 160);
        }

        // Greedy state-recovery loop
        bool recovering = true;
        while (recovering) {
            recovering = false;
            
            // Sweep a local sliding window to check for solvable XOR combinations
            int start = std::max(0LL, host_seq - 5LL);
            for (int s = start; s <= host_seq + 2; s++) {
                if (has_parity[s]) {
                    int p1 = s - 1;
                    int p2 = s - 2;
                    
                    if (p1 >= 0 && p2 >= 0) {
                        // If we have p1 but are missing p2, deduce p2
                        if (seen[p1] && !seen[p2]) {
                            unsigned char recovered[160];
                            for(int k = 0; k < 160; k++) recovered[k] = parities[s][k] ^ payloads[p1][k];
                            fire_packet(p2, recovered, out_fd, player);
                            recovering = true; // A recovery might unlock adjacent missing frames
                        }
                        // If we have p2 but are missing p1, deduce p1
                        else if (!seen[p1] && seen[p2]) {
                            unsigned char recovered[160];
                            for(int k = 0; k < 160; k++) recovered[k] = parities[s][k] ^ payloads[p2][k];
                            fire_packet(p1, recovered, out_fd, player);
                            recovering = true;
                        }
                    }
                }
            }
        }
    }
    return 0;
}
