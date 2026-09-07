# bds-ipv6fix: CI and release behavior

- A beta prerelease is created automatically on GitHub for every commit to main that touches `bds-ipv6fix.c`, `test.c`, or `.github/workflows/beta.yml` (paths filter in beta.yml). One beta per source commit is intentional; do not treat betas as noise or suggest deleting them. Flag only a missing beta, or a non-source commit that triggered one. v-tagged releases are the stable channel; betas are the dev artifact.
- The shim intercepts bind() only; there is no setsockopt hook. Linux forbids setting IPV6_V6ONLY after bind (verified by test), so the bind-time getsockopt check is guaranteed to catch a future native BDS fix.
- CI gates every beta and release on functional tests: test.c dual-bind (must fail bare, pass preloaded, log line asserted), native-fix redundancy mode, and the aarch64 .so run under qemu-user.
- The version string is injected via -DBDS_IPV6FIX_VERSION (tag name on release, beta-<timestamp> on beta, "dev" locally); the startup log line shows it.
- Release asset naming is x86_64/aarch64.
- Sources are ASCII-only.
