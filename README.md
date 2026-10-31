# bds-ipv6fix

LD_PRELOAD shim for [Minecraft Bedrock Dedicated Server](https://github.com/itzg/docker-minecraft-bedrock-server) that fixes a known BDS limitation where IPv4 and IPv6 cannot be configured on the same port. Only needed with `transport=raknet`; see [NetherNet](#nethernet).

When the fix is active and working, you will see a line like this in your server log:

```
[bds-ipv6fix] Fixing IPv6: IPV6_V6ONLY=1 set on fd=... port=...
```

The shim is safe to leave in place across BDS upgrades. If a future BDS version fixes this issue on its own, the shim detects it automatically and logs a `NOTE: ... patch is now redundant` message - it will not interfere with the server.

See the full technical description in [bds-ipv6fix.c](bds-ipv6fix.c).

## NetherNet

BDS 1.26.50 and later defaults to `transport=nethernet`. NetherNet signaling uses one dual-stack TCP socket on `server-port` for IPv4 and IPv6 clients, and `server-portv6` is ignored. Same-port dual-stack works natively there. The fix is only needed for `transport=raknet`.

The shim acts on UDP (SOCK_DGRAM) sockets only and never touches the NetherNet TCP socket. It can stay enabled on a NetherNet server, even with `SERVER_PORT_V6` equal to `SERVER_PORT`. It says so at startup and whenever it skips a non-UDP socket:

```
[bds-ipv6fix] RakNet-only fix: acts on UDP (SOCK_DGRAM) sockets only, NetherNet TCP signaling is left untouched
[bds-ipv6fix] NOTE: fd=... port=... is not a UDP socket (NetherNet TCP signaling?) - RakNet-only fix, leaving it untouched
```

Keep the `server-udp-ports` range clear of `SERVER_PORT_V6`. A NetherNet gameplay UDP socket on that port would be treated as the RakNet IPv6 socket.

## Usage

Consumed by [itzg/docker-minecraft-bedrock-server](https://github.com/itzg/docker-minecraft-bedrock-server) via `ENABLE_BDS_V6BIND_FIX=true`. Pre-built binaries for `x86_64` and `aarch64` are available on the [releases page](../../releases).

The itzg image sets `LD_PRELOAD` to the shim when `ENABLE_BDS_V6BIND_FIX=true` is set.

To use the shim outside of the itzg image:

```sh
curl -fsSL "https://github.com/poeggi/bds-ipv6fix/releases/latest/download/bds-ipv6fix_linux_$(uname -m).so" \
  -o bds-ipv6fix.so
export LD_PRELOAD=$(pwd)/bds-ipv6fix.so
exec ./bedrock_server
```

## Building

```sh
# amd64
gcc -shared -fPIC -O2 -o bds-ipv6fix_linux_x86_64.so bds-ipv6fix.c -ldl

# aarch64 (cross-compile)
aarch64-linux-gnu-gcc -shared -fPIC -O2 -o bds-ipv6fix_linux_aarch64.so bds-ipv6fix.c -ldl
```

## Testing

Every CI build is gated on a functional test: [test.c](test.c) reproduces the
BDS dual-bind failure (must fail without the shim) and verifies the shim fixes
it, for both architectures (aarch64 runs under qemu-user). A further case mimics
the NetherNet dual-stack TCP socket and proves the shim leaves it alone. To run
it locally:

```sh
gcc -O2 -o test test.c
./test                                                       # must fail: reproduces the bug
LD_PRELOAD=$PWD/bds-ipv6fix_linux_x86_64.so ./test           # must pass: shim fixes it
LD_PRELOAD=$PWD/bds-ipv6fix_linux_x86_64.so ./test nethernet # must pass: TCP socket untouched
```

## Dear Mojang/Microsoft

This shim exists because `bedrock_server` with `transport=raknet` binds its IPv6 socket without first calling `setsockopt(IPV6_V6ONLY, 1)`. The socket therefore inherits the host kernel's `net.ipv6.bindv6only` default (0 on most Linux systems), which means the IPv6 socket absorbs IPv4-mapped traffic as well. When a user sets `SERVER_PORT` and `SERVER_PORT_V6` to the same value, the subsequent IPv4 bind fails with `EADDRINUSE` and BDS segfaults. NetherNet gets this right with one dual-stack socket on `server-port`.

The native fix is a one-liner between `socket()` and `bind()` in the IPv6 socket setup path:

```c
int one = 1;
setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
```

This makes the IPv6 socket strictly IPv6-only, allows both address families to share a port, and makes this shim unnecessary. The Bedrock client does not implement Happy Eyeballs (RFC 8305), so players whose DNS e.g. returns IPv6 first will silently time out if the client was configured to connect to the port only valid for IPv4 (and vice-versa) -> same-port dual-stack is the best we can do to fix this on the server side.

Additional Happy Eyeballs on client side _still_ considered beneficial, as it would mitigate all other dual-stack issues in the network path between client and server.

NetherNet does not change that. The signaling socket is dual-stack, but the client still sends exactly one request per connection attempt and does not retry, so there is still no Happy Eyeballs. Gameplay gathers host ICE candidates only, and the NetherNet onboarding guide shows IPv4 candidates only. A client that reaches the server over IPv6 but gets no reachable IPv6 gameplay candidate still ends up with a timeout. Hello Mojang/Microsoft: please add Happy Eyeballs to the client.

## License

MIT

## Authors

[poeggi](https://github.com/poeggi) with [Claude](https://claude.ai) (Anthropic)
