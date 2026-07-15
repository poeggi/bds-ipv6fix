/*
 * CI test for bds-ipv6fix: binds AF_INET6 then AF_INET on the same port,
 * reproducing the BDS socket setup that triggers the bug.
 *
 * Without the shim the second bind fails with EADDRINUSE (exit 1).
 * With the shim preloaded both binds succeed (exit 0).
 *
 * With argument "native-fix" the test sets IPV6_V6ONLY itself before bind,
 * simulating a future BDS version that fixes the bug natively; the shim
 * must then log that the patch is redundant and stay out of the way.
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define TEST_PORT 19133

int main(int argc, char **argv) {
    int native_fix = argc > 1 && strcmp(argv[1], "native-fix") == 0;

    int fd6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in6 sa6 = {0};
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(TEST_PORT);
    if (native_fix) {
        int one = 1;
        if (fd6 < 0 || setsockopt(fd6, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one)) != 0) {
            fprintf(stderr, "FAIL: presetting IPV6_V6ONLY: %s\n", strerror(errno));
            return 1;
        }
    }
    if (fd6 < 0 || bind(fd6, (struct sockaddr *)&sa6, sizeof(sa6)) != 0) {
        fprintf(stderr, "FAIL: IPv6 bind: %s\n", strerror(errno));
        return 1;
    }

    int fd4 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in sa4 = {0};
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(TEST_PORT);
    if (fd4 < 0 || bind(fd4, (struct sockaddr *)&sa4, sizeof(sa4)) != 0) {
        fprintf(stderr, "FAIL: IPv4 bind on same port: %s\n", strerror(errno));
        return 1;
    }

    fprintf(stderr, "PASS: IPv4 and IPv6 bound to port %d\n", TEST_PORT);
    return 0;
}
