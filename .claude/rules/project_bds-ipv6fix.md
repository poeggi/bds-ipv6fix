# bds-ipv6fix: project rules

- Every commit to main that touches `bds-ipv6fix.c`, `test.c` or `.github/workflows/beta.yml` creates a beta prerelease. One beta per source commit is intended; do not treat betas as noise or suggest deleting them. Flag only a missing beta, or a beta from a non-source commit. v-tags are the stable channel.
- The shim intercepts bind() only. When the bind of a dual-stack IPv6 UDP socket fails with EADDRINUSE, it sets IPV6_V6ONLY and binds once more, and puts the option back if that fails too. Linux allows IPV6_V6ONLY after a failed bind, not after a successful one.
- This relies on BDS RakNet binding IPv4 first, IPv6 second (verified on BDS 1.19.83 to 1.26.62, on dual-stack, IPv4-only and IPv6-only hosts). A BDS that binds IPv6 first is not fixed.
- The only configuration is the environment variable `SERVER_PORT_V6`: set, the shim acts on that port only; unset or invalid, on any port, and the startup line says which. No transport detection and no server.properties; do not add any. A TCP listener on the game port does not prove NetherNet; the script debugger can open one on a RakNet server.
- CI gates every beta and release on test.c. The default case must fail bare and pass preloaded with the fix line logged. `lone`, `v6-holder`, `v6only` and `tcp` must pass with only the startup line logged. The aarch64 build runs under qemu-user.
- The version string comes from -DBDS_IPV6FIX_VERSION (tag name, beta-<timestamp>, or "dev" locally) and shows in the startup log line.
- Release assets are named x86_64 and aarch64.
- Sources are ASCII-only.
