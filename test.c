/*
 * CI test for bds-ipv6fix. Each case binds a dual-stack IPv6 socket to a
 * port that another socket may hold, then checks the bind result and
 * IPV6_V6ONLY.
 *
 * Without an argument the holder is an IPv4 UDP socket, as in BDS RakNet
 * with equal ports: that fails without the shim (exit 1) and passes with it.
 * The named cases pass with and without the shim, which must log only its
 * startup line.
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define TEST_PORT 19133

static const struct {
    const char *name;
    int holder; /* address family of the socket holding the port, 0 for none */
    int type;
    int preset; /* IPV6_V6ONLY on the test socket before bind */
    int bind_ok;
    int v6only; /* IPV6_V6ONLY expected after bind */
} cases[] = {
    { "",          AF_INET,  SOCK_DGRAM,  0, 1, 1 },
    { "lone",      0,        SOCK_DGRAM,  0, 1, 0 },
    { "v6-holder", AF_INET6, SOCK_DGRAM,  0, 0, 0 },
    { "v6only",    AF_INET6, SOCK_DGRAM,  1, 0, 1 },
    { "tcp",       AF_INET,  SOCK_STREAM, 0, 0, 0 },
};

static int bind_port(int fd, int family) {
    struct sockaddr_in sa4 = {0};
    struct sockaddr_in6 sa6 = {0};
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(TEST_PORT);
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(TEST_PORT);
    return family == AF_INET ? bind(fd, (struct sockaddr *)&sa4, sizeof(sa4))
                             : bind(fd, (struct sockaddr *)&sa6, sizeof(sa6));
}

int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : "";
    const size_t count = sizeof(cases) / sizeof(cases[0]);
    int one = 1, v6only = -1;
    socklen_t len = sizeof(v6only);
    size_t i;

    for (i = 0; i < count && strcmp(cases[i].name, name) != 0; i++)
        ;
    if (i == count) {
        fprintf(stderr, "unknown case: %s\n", name);
        return 2;
    }

    if (cases[i].holder) {
        int holder = socket(cases[i].holder, cases[i].type, 0);
        if (cases[i].holder == AF_INET6)
            setsockopt(holder, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
        if (bind_port(holder, cases[i].holder) != 0) {
            fprintf(stderr, "FAIL: holder bind: %s\n", strerror(errno));
            return 1;
        }
    }

    int fd = socket(AF_INET6, cases[i].type, 0);
    /* Explicit, so the result does not depend on net.ipv6.bindv6only. */
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &cases[i].preset, sizeof(cases[i].preset));
    int rc = bind_port(fd, AF_INET6);
    if (cases[i].bind_ok ? rc != 0 : (rc == 0 || errno != EADDRINUSE)) {
        fprintf(stderr, "FAIL: IPv6 bind: %s\n", rc ? strerror(errno) : "succeeded on a taken port");
        return 1;
    }

    getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &len);
    if (v6only != cases[i].v6only) {
        fprintf(stderr, "FAIL: IPV6_V6ONLY=%d after bind, expected %d\n", v6only, cases[i].v6only);
        return 1;
    }
    fprintf(stderr, "PASS\n");
    return 0;
}
