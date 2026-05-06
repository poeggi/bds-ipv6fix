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
 * BDS opens its IPv6 socket without calling setsockopt(IPV6_V6ONLY):
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
 * Intercepts bind() and setsockopt() via LD_PRELOAD.
 *
 * On any AF_INET6 bind to the target port, checks whether IPV6_V6ONLY is
 * already set (BDS natively fixed it before bind), then either logs that the
 * patch is redundant or calls setsockopt(IPV6_V6ONLY, 1) to apply the fix.
 *
 * The setsockopt() intercept catches the case where BDS calls setsockopt after
 * bind — if it sets IPV6_V6ONLY=1 on the tracked target fd, the patch is
 * reported as redundant.
 *
 * ARM64 / BOX64
 * -------------
 * On ARM64, BDS (x86_64) runs under box64, which forwards libc calls to the
 * host ARM64 libc. An ARM64 build of this shim intercepts those calls.
 *
 * FAILURE MODES
 * -------------
 * - .so fails to load: dynamic linker warns, BDS starts with original behaviour.
 * - dlsym("setsockopt") returns NULL: warned at startup, IPV6_V6ONLY not set.
 * - setsockopt fails at runtime: warning with errno logged, bind() still called.
 * - dlsym("bind") returns NULL: warned, raw syscall fallback used.
 *
 * CONFIGURATION
 * -------------
 * Reads SERVER_PORT_V6 from the environment at startup. Must be a decimal
 * integer in 1-65535; invalid values are warned and the default (19133) is
 * used. Always set SERVER_PORT_V6 via environment — not server.properties —
 * when this shim is active.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#define BDS_DEFAULT_PORT_V6 19133

static int (*real_bind)(int, const struct sockaddr *, socklen_t)       = NULL;
static int (*real_setsockopt)(int, int, int, const void *, socklen_t)  = NULL;

static int target_port      = BDS_DEFAULT_PORT_V6;
static int target_ipv6_fd   = -1;
static int target_ipv6_port = -1;

__attribute__((constructor))
static void bds_ipv6fix_init(void) {
    real_bind       = dlsym(RTLD_NEXT, "bind");
    real_setsockopt = dlsym(RTLD_NEXT, "setsockopt");

    if (!real_bind)
        fprintf(stderr, "[bds-ipv6fix] WARNING: could not resolve bind — using raw syscall fallback\n");
    if (!real_setsockopt)
        fprintf(stderr, "[bds-ipv6fix] WARNING: could not resolve setsockopt — IPV6_V6ONLY fix will not apply\n");

    const char *pv6 = getenv("SERVER_PORT_V6");
    if (pv6) {
        char *end;
        long val = strtol(pv6, &end, 10);
        if (*end != '\0' || val < 1 || val > 65535)
            fprintf(stderr, "[bds-ipv6fix] WARNING: invalid SERVER_PORT_V6=\"%s\", using default %d\n",
                    pv6, BDS_DEFAULT_PORT_V6);
        else
            target_port = (int)val;
    }

    fprintf(stderr, "[bds-ipv6fix] active, SERVER_PORT_V6=%d (default=%d)\n",
            target_port, BDS_DEFAULT_PORT_V6);
}

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen) {
    if (fd == target_ipv6_fd &&
        level == IPPROTO_IPV6 && optname == IPV6_V6ONLY &&
        optlen >= (socklen_t)sizeof(int) && *(const int *)optval) {
        fprintf(stderr, "[bds-ipv6fix] NOTE: BDS set IPV6_V6ONLY on fd=%d port=%d"
                " — patch is now redundant\n", fd, target_ipv6_port);
    }
    return real_setsockopt ? real_setsockopt(fd, level, optname, optval, optlen)
                           : (int)syscall(SYS_setsockopt, fd, level, optname, optval, optlen);
}

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    if (addr->sa_family == AF_INET6 && len >= (socklen_t)sizeof(struct sockaddr_in6)) {
        int port = (int)ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);
        if (port == BDS_DEFAULT_PORT_V6 || port == target_port) {
            target_ipv6_fd   = fd;
            target_ipv6_port = port;
            int cur = 0;
            socklen_t curlen = sizeof(cur);
            if (getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &cur, &curlen) == 0 && cur) {
                fprintf(stderr, "[bds-ipv6fix] NOTE: IPV6_V6ONLY already set on fd=%d port=%d"
                        " — BDS has fixed this natively; patch is now redundant\n", fd, port);
            } else if (real_setsockopt) {
                int one = 1;
                int rc = real_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
                if (rc == 0)
                    fprintf(stderr, "[bds-ipv6fix] Fixing IPv6: IPV6_V6ONLY=1 set on fd=%d port=%d\n", fd, port);
                else
                    fprintf(stderr, "[bds-ipv6fix] WARNING: setsockopt(IPV6_V6ONLY) failed on fd=%d port=%d: %s\n",
                            fd, port, strerror(errno));
            }
        }
    }
    if (real_bind)
        return real_bind(fd, addr, len);
    return (int)syscall(SYS_bind, fd, addr, len);
}
