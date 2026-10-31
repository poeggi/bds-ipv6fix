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
 *
 * With argument "nethernet" the test mimics the NetherNet signaling socket:
 * one dual-stack TCP listener on the port, serving IPv4 and IPv6 alike. The
 * shim must leave it untouched. The test checks IPV6_V6ONLY after bind, then
 * proves the point by connecting over IPv4 and accepting the connection
 * (exit 0). A forced IPV6_V6ONLY fails the check, and would refuse the IPv4
 * connection as well (exit 1).
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TEST_PORT 19133

static int nethernet_case(void) {
    int zero = 0, one = 1;
    int lfd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (lfd < 0
        || setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero)) != 0
        || setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        fprintf(stderr, "FAIL: dual-stack TCP socket setup: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in6 sa6 = {0};
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(TEST_PORT);
    if (bind(lfd, (struct sockaddr *)&sa6, sizeof(sa6)) != 0 || listen(lfd, 1) != 0) {
        fprintf(stderr, "FAIL: dual-stack TCP listen: %s\n", strerror(errno));
        return 1;
    }

    int v6only = -1;
    socklen_t len = sizeof(v6only);
    if (getsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &len) != 0 || v6only != 0) {
        fprintf(stderr, "FAIL: IPV6_V6ONLY=%d on the TCP socket, IPv4 clients are locked out\n", v6only);
        return 1;
    }

    int cfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sa4 = {0};
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(TEST_PORT);
    sa4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (cfd < 0 || connect(cfd, (struct sockaddr *)&sa4, sizeof(sa4)) != 0) {
        fprintf(stderr, "FAIL: IPv4 connect to the dual-stack TCP socket: %s\n", strerror(errno));
        return 1;
    }

    int afd = accept(lfd, NULL, NULL);
    if (afd < 0) {
        fprintf(stderr, "FAIL: accept: %s\n", strerror(errno));
        return 1;
    }

    /* Client closes first so the listener side never lingers in TIME_WAIT. */
    close(cfd);
    close(afd);
    close(lfd);
    fprintf(stderr, "PASS: dual-stack TCP socket on port %d serves IPv4\n", TEST_PORT);
    return 0;
}

int main(int argc, char **argv) {
    int native_fix = argc > 1 && strcmp(argv[1], "native-fix") == 0;

    if (argc > 1 && strcmp(argv[1], "nethernet") == 0)
        return nethernet_case();

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
