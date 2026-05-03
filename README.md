# bds-ipv6fix

LD_PRELOAD shim for [Minecraft Bedrock Dedicated Server](https://github.com/itzg/docker-minecraft-bedrock-server) that fixes a known BDS limitation where IPv4 and IPv6 cannot be configured on the same port.

See the full technical description in [bds-ipv6fix.c](bds-ipv6fix.c).

## Usage

Consumed by [itzg/docker-minecraft-bedrock-server](https://github.com/itzg/docker-minecraft-bedrock-server) via `ENABLE_BDS_V6BIND_FIX=true`. Pre-built binaries for `amd64` and `arm64` are available on the [releases page](../../releases).

To use the shim outside of the itzg image, set `LD_PRELOAD` before launching BDS:

```sh
export LD_PRELOAD=/path/to/bds-ipv6fix_linux_amd64.so
exec ./bedrock_server
```

The itzg image handles this automatically when `ENABLE_BDS_V6BIND_FIX=true` is set:

```sh
if [ "$ENABLE_BDS_V6BIND_FIX" = "true" ]; then
  export LD_PRELOAD=/usr/local/lib/bds-ipv6fix.so
fi
exec ./bedrock_server
```

## Building

```sh
# amd64
gcc -shared -fPIC -O2 -o bds-ipv6fix_linux_amd64.so bds-ipv6fix.c -ldl

# arm64 (cross-compile)
aarch64-linux-gnu-gcc -shared -fPIC -O2 -o bds-ipv6fix_linux_arm64.so bds-ipv6fix.c -ldl
```

## Dear Mojang/Microsoft

This shim exists because `bedrock_server` binds its IPv6 socket without first calling `setsockopt(IPV6_V6ONLY, 1)`. The socket therefore inherits the host kernel's `net.ipv6.bindv6only` default (0 on most Linux systems), which means the IPv6 socket absorbs IPv4-mapped traffic as well. When a user sets `SERVER_PORT` and `SERVER_PORT_V6` to the same value, the subsequent IPv4 bind fails with `EADDRINUSE` and BDS segfaults.

The native fix is a one-liner between `socket()` and `bind()` in the IPv6 socket setup path:

```c
int one = 1;
setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
```

This makes the IPv6 socket strictly IPv6-only, allows both address families to share a port, and makes this shim unnecessary. The Bedrock client does not implement Happy Eyeballs (RFC 8305), so players whose DNS returns IPv6 first will silently time out if the server only listens on IPv4 — same-port dual-stack is the correct fix on the server side.

## License

MIT

## Authors

[poeggi](https://github.com/poeggi) with [Claude](https://claude.ai) (Anthropic)
