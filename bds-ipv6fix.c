/*
 * LD_PRELOAD shim for Minecraft Bedrock Dedicated Server (BDS). Lets
 * server-port and server-portv6 share one port with transport=raknet.
 *
 * BDS binds its IPv4 UDP socket first and its IPv6 UDP socket second. The
 * IPv6 socket is dual-stack, so on the same port its bind fails with
 * EADDRINUSE and BDS terminates.
 *
 * The shim passes every bind() to the kernel. When the bind of a dual-stack
 * IPv6 UDP socket fails with EADDRINUSE, it sets IPV6_V6ONLY and binds once
 * more. That succeeds only if an IPv4 socket holds the port; otherwise the
 * shim puts the option back and returns the original error.
 *
 * With SERVER_PORT_V6 set in the environment the shim acts on that port
 * only, otherwise on any port.
 *
 * Linux refuses IPV6_V6ONLY on a bound socket. A failed bind leaves the
 * socket unbound, so the option can still be set then. The shim replaces the
 * libc bind symbol and reaches the kernel through syscall(): dlsym would
 * need a libc symbol newer than GLIBC_2.17, the oldest glibc the builds
 * load on.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef BDS_IPV6FIX_VERSION
#define BDS_IPV6FIX_VERSION "dev"
#endif

/* The one port to act on, or 0 for any port. */
static int only_port;

/* Decimal port 1-65535, or 0. Hand-rolled: strtol maps to a glibc symbol
 * newer than GLIBC_2.17. */
static int parse_port(const char *s) {
    int val = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return 0;
        val = val * 10 + (*s - '0');
        if (val > 65535)
            return 0;
    }
    return val;
}

__attribute__((constructor))
static void bds_ipv6fix_init(void) {
    const char *env = getenv("SERVER_PORT_V6");
    only_port = env ? parse_port(env) : 0;
    if (only_port)
        fprintf(stderr, "[bds-ipv6fix] " BDS_IPV6FIX_VERSION " active on port %d (SERVER_PORT_V6)\n", only_port);
    else
        fprintf(stderr, "[bds-ipv6fix] " BDS_IPV6FIX_VERSION " active on any port (SERVER_PORT_V6 %s)\n",
                env ? "invalid" : "not set");
}

/* UDP only: the NetherNet TCP listener must stay dual-stack. */
static int is_dual_stack_udp(int fd) {
    int type = 0, v6only = 0;
    socklen_t len = sizeof(type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0 || type != SOCK_DGRAM)
        return 0;
    len = sizeof(v6only);
    return getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &len) == 0 && !v6only;
}

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    int saved = errno;
    int rc = (int)syscall(SYS_bind, fd, addr, len);

    /* EADDRINUSE implies the kernel has read and accepted addr. */
    if (rc == 0 || errno != EADDRINUSE || addr->sa_family != AF_INET6)
        return rc;

    int port = (int)ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);
    if ((!only_port || port == only_port) && is_dual_stack_udp(fd)) {
        int on = 1, off = 0;
        int failed = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
        if (!failed) {
            if (syscall(SYS_bind, fd, addr, len) == 0) {
                fprintf(stderr, "[bds-ipv6fix] Fixing IPv6: IPV6_V6ONLY=1 set on fd=%d port=%d"
                        " (bind collided with an IPv4 socket)\n", fd, port);
                errno = saved;
                return 0;
            }
            failed = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
        }
        if (failed)
            fprintf(stderr, "[bds-ipv6fix] WARNING: setsockopt(IPV6_V6ONLY) failed on fd=%d port=%d: %s\n",
                    fd, port, strerror(errno));
    }
    errno = EADDRINUSE;
    return -1;
}
