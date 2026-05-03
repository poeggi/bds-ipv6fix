# bds-ipv6fix

LD_PRELOAD shim for [Minecraft Bedrock Dedicated Server](https://github.com/itzg/docker-minecraft-bedrock-server) that fixes a known limitation where IPv4 and IPv6 cannot be configured on the same port.

See the full technical description in [bds-ipv6fix.c](bds-ipv6fix.c).

## Usage

Consumed by [itzg/docker-minecraft-bedrock-server](https://github.com/itzg/docker-minecraft-bedrock-server) via `ENABLE_BDS_V6BIND_FIX=true`. Pre-built binaries for `amd64` and `arm64` are available on the [releases page](../../releases).

## Building

```sh
# amd64
gcc -shared -fPIC -O2 -o bds-ipv6fix_linux_amd64.so bds-ipv6fix.c -ldl

# arm64 (cross-compile)
aarch64-linux-gnu-gcc -shared -fPIC -O2 -o bds-ipv6fix_linux_arm64.so bds-ipv6fix.c -ldl
```

## License

MIT

## Authors

[poeggi](https://github.com/poeggi) with [Claude](https://claude.ai) (Anthropic)
