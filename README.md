# bds-ipv6fix

LD_PRELOAD shim for [Minecraft Bedrock Dedicated Server](https://github.com/itzg/docker-minecraft-bedrock-server) that fixes a known BDS RakNet limitation where IPv4 and IPv6 cannot be configured on the same port. Only needed with `transport=raknet`; not needed with [NetherNet](#nethernet) (the latest Minecraft Bedrock default!).

The shim acts on RakNet (UDP sockets) only. It never touches TCP by design.

When the fix is active and working, you will see a line like this in your server log:

```
[bds-ipv6fix] Fixing IPv6: IPV6_V6ONLY=1 set on fd=... port=... (UDP bind collided with an IPv4 socket)
```

The shim is safe to leave in place across BDS upgrades - it will not interfere with the server if not needed.

## How it works

At startup the shim reads `SERVER_PORT_V6` from the environment:

| `SERVER_PORT_V6` | Shim acts on | Startup line |
|---|---|---|
| valid port | that port only | `active: RakNet fix, UDP only, on port <port> (SERVER_PORT_V6)` |
| not set | any port | `active: RakNet fix, UDP only, on any port (SERVER_PORT_V6 not set)` |
| invalid | any port | `active: RakNet fix, UDP only, on any port (SERVER_PORT_V6 invalid)` |

Every `bind()` goes to the kernel first. What follows depends on the result:

| State | Condition | Shim does |
|---|---|---|
| Bound | the kernel accepted the bind | nothing |
| Failed | any error other than `EADDRINUSE` | returns the error |
| Collision | not a dual-stack IPv6 UDP socket, or not a port it acts on | returns the error |
| Collision | dual-stack IPv6 UDP socket on a port it acts on | sets `IPV6_V6ONLY` and binds again |
| Retry | succeeds: an IPv4 socket held the port | logs the fix, returns success |
| Retry | fails: an IPv6 socket holds the port | puts the option back, returns the error |

BDS only ever sees the final result.

## NetherNet

With `transport=nethernet`, BDS serves IPv4 and IPv6 on `server-port` natively.
The shim can stay enabled there. It never touches the TCP listener.

## Usage

Set `server-port` and `server-portv6` to the same port.

With [itzg/docker-minecraft-bedrock-server](https://github.com/itzg/docker-minecraft-bedrock-server): set `ENABLE_BDS_V6BIND_FIX=true` and give `SERVER_PORT_V6` the value of `SERVER_PORT`.

Without the image, use the `x86_64` or `aarch64` binary from the [releases page](../../releases):

```sh
curl -fsSL "https://github.com/poeggi/bds-ipv6fix/releases/latest/download/bds-ipv6fix_linux_$(uname -m).so" \
  -o bds-ipv6fix.so
export LD_PRELOAD=$(pwd)/bds-ipv6fix.so
exec ./bedrock_server
```

## Building

```sh
# amd64
gcc -shared -fPIC -O2 -o bds-ipv6fix_linux_x86_64.so bds-ipv6fix.c

# aarch64 (cross-compile)
aarch64-linux-gnu-gcc -shared -fPIC -O2 -o bds-ipv6fix_linux_aarch64.so bds-ipv6fix.c
```

One build per architecture loads on any glibc from 2.17 on.

## Testing

[test.c](test.c) reproduces the BDS bind failure and verifies the fix.
CI runs it on every build, for both architectures.

```sh
gcc -O2 -o test test.c
./test                                              # must fail: reproduces the bug
LD_PRELOAD=$PWD/bds-ipv6fix_linux_x86_64.so ./test  # must pass: shim fixes it
```

## Dear Mojang/Microsoft

This shim exists because `bedrock_server` with `transport=raknet` binds its IPv6 socket without first calling `setsockopt(IPV6_V6ONLY, 1)`. The socket therefore inherits the host kernel's `net.ipv6.bindv6only` default (0 on most Linux systems), which means the IPv6 socket absorbs IPv4-mapped traffic as well. BDS binds its IPv4 socket first. When a user sets `server-port` and `server-portv6` to the same value, the IPv6 bind fails with `EADDRINUSE` and BDS terminates. NetherNet gets this right with one dual-stack socket on `server-port`.

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
