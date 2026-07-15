/*
 * CI test for bds-ipv6fix: binds AF_INET6 then AF_INET on the same port,
 * reproducing the BDS socket setup that triggers the bug.
 *
 * Without the shim the second bind fails with EADDRINUSE (exit 1).
 * With the shim preloaded both binds succeed (exit 0).
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define TEST_PORT 19133

int main(void) {
    int fd6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in6 sa6 = {0};
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(TEST_PORT);
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
