/*
 * LD_PRELOAD shim for Minecraft Bedrock Dedicated Server (BDS).
 *
 * PURPOSE
 * -------
 * Allows SERVER_PORT and SERVER_PORT_V6 to use the same port so both IPv4
 * and IPv6 clients can reach the server without port mismatch.
 *
 * USER-VISIBLE PROBLEM
 * --------------------
 * BDS defaults to IPv4 on 19132 and IPv6 on 19133. Bedrock clients don't
 * implement Happy Eyeballs (RFC 8305): they connect on whichever address
 * family DNS returns first with no fallback. Players whose devices resolve
 * the hostname to IPv6 try port 19132 over IPv6, find nothing, and time out.
 *
 * ROOT CAUSE IN BDS
 * -----------------
 * With transport=raknet, BDS opens its IPv6 socket without calling
 * setsockopt(IPV6_V6ONLY):
 *
 *   socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP) = 8
 *   bind(8, {sa_family=AF_INET6, sin6_port=htons(19133), "::"}, 28) = 0
 *
 * The socket inherits net.ipv6.bindv6only (0 on most Linux systems), making
 * it dual-stack. When both ports are equal, the subsequent AF_INET bind on
 * the same port gets EADDRINUSE and BDS segfaults.
 *
 * THE FIX
 * -------
 * Intercepts bind() via LD_PRELOAD.
 *
 * On any AF_INET6 UDP (SOCK_DGRAM) bind to the target port, checks whether
 * IPV6_V6ONLY is already set (BDS natively fixed it before bind), then either
 * logs that the patch is redundant or calls setsockopt(IPV6_V6ONLY, 1) to
 * apply the fix. Linux only allows setting IPV6_V6ONLY before bind, so this
 * check is guaranteed to catch a future native BDS fix.
 *
 * Sockets of any other type on the target port are logged and left untouched.
 *
 * NETHERNET
 * ---------
 * BDS 1.26.50 and later defaults to transport=nethernet. NetherNet signaling
 * uses one dual-stack TCP socket on server-port for both address families and
 * ignores server-portv6, so same-port dual-stack works natively there. The
 * fix only matters for transport=raknet. The SOCK_DGRAM guard keeps the shim
 * away from the NetherNet TCP socket: forcing IPV6_V6ONLY on it would lock
 * out IPv4 clients. The shim can therefore stay enabled on a NetherNet
 * server, even with SERVER_PORT_V6 equal to SERVER_PORT.
 *
 * ARM64 / BOX64
 * -------------
 * On ARM64, BDS (x86_64) runs under box64, which forwards libc calls to the
 * host ARM64 libc. An ARM64 build of this shim intercepts those calls.
 *
 * PORTABILITY
 * -----------
 * The shim replaces the libc bind symbol, so its own bind() must not call
 * bind again; it goes through the raw syscall interface instead of dlsym
 * (which would need GLIBC_2.34). The port is parsed by hand because strtol
 * maps to a GLIBC_2.38 symbol on newer glibc. The shim therefore needs no
 * libc symbol newer than GLIBC_2.17, and one build per architecture loads
 * on any glibc from 2.17 on. CI checks the required symbol versions and runs
 * the x86_64 build on Debian 12.
 *
 * FAILURE MODES
 * -------------
 * - .so fails to load: dynamic linker warns, BDS starts with original behaviour.
 * - getsockopt(SO_TYPE) fails: warned, socket left untouched, bind() still called.
 * - setsockopt fails at runtime: warning with errno logged, bind() still called.
 *
 * CONFIGURATION
 * -------------
 * Reads SERVER_PORT_V6 from the environment at startup. Must be a decimal
 * integer in 1-65535; invalid values are warned and the default (19133) is
 * used. Always set SERVER_PORT_V6 via environment - not server.properties -
 * when this shim is active.
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

#define BDS_DEFAULT_PORT_V6 19133

#ifndef BDS_IPV6FIX_VERSION
#define BDS_IPV6FIX_VERSION "dev"
#endif

static int target_port = BDS_DEFAULT_PORT_V6;

/* Decimal port in 1-65535, or -1. Hand-rolled because strtol maps to a
 * versioned symbol on newer glibc and would tie the build to that glibc. */
static int parse_port(const char *s) {
    long val = 0;
    if (*s == '\0')
        return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        val = val * 10 + (*s - '0');
        if (val > 65535)
            return -1;
    }
    return val >= 1 ? (int)val : -1;
}

__attribute__((constructor))
static void bds_ipv6fix_init(void) {
    const char *pv6 = getenv("SERVER_PORT_V6");
    if (pv6) {
        int val = parse_port(pv6);
        if (val < 0)
            fprintf(stderr, "[bds-ipv6fix] WARNING: invalid SERVER_PORT_V6=\"%s\", using default %d\n",
                    pv6, BDS_DEFAULT_PORT_V6);
        else
            target_port = val;
    }

    fprintf(stderr, "[bds-ipv6fix] " BDS_IPV6FIX_VERSION " active, SERVER_PORT_V6=%d (default=%d)\n",
            target_port, BDS_DEFAULT_PORT_V6);
    fprintf(stderr, "[bds-ipv6fix] RakNet-only fix: acts on UDP (SOCK_DGRAM) sockets only,"
            " NetherNet TCP signaling is left untouched\n");
}

/* The RakNet IPv6 socket is UDP. Anything else on the target port (the
 * NetherNet TCP signaling socket in particular) must not be modified. */
static int is_udp_socket(int fd, int port) {
    int type = 0;
    socklen_t typelen = sizeof(type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &typelen) != 0) {
        fprintf(stderr, "[bds-ipv6fix] WARNING: cannot read socket type of fd=%d port=%d: %s"
                " - leaving it untouched\n", fd, port, strerror(errno));
        return 0;
    }
    if (type != SOCK_DGRAM) {
        fprintf(stderr, "[bds-ipv6fix] NOTE: fd=%d port=%d is not a UDP socket (NetherNet TCP signaling?)"
                " - RakNet-only fix, leaving it untouched\n", fd, port);
        return 0;
    }
    return 1;
}

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    if (addr && len >= (socklen_t)sizeof(struct sockaddr_in6) && addr->sa_family == AF_INET6) {
        int port = (int)ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);
        if (port == target_port) {
            int saved = errno;
            if (is_udp_socket(fd, port)) {
                int cur = 0;
                socklen_t curlen = sizeof(cur);
                int already = (getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &cur, &curlen) == 0 && cur);
                errno = saved;
                if (already) {
                    fprintf(stderr, "[bds-ipv6fix] NOTE: IPV6_V6ONLY already set on fd=%d port=%d"
                            " - BDS has fixed this natively; patch is now redundant\n", fd, port);
                } else {
                    int one = 1;
                    int rc = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
                    if (rc == 0)
                        fprintf(stderr, "[bds-ipv6fix] Fixing IPv6: IPV6_V6ONLY=1 set on fd=%d port=%d\n", fd, port);
                    else
                        fprintf(stderr, "[bds-ipv6fix] WARNING: setsockopt(IPV6_V6ONLY) failed on fd=%d port=%d: %s\n",
                                fd, port, strerror(errno));
                }
            } else {
                errno = saved;
            }
        }
    }
    return (int)syscall(SYS_bind, fd, addr, len);
}
