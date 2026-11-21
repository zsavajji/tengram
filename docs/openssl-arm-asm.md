# OpenSSL with the ARM assembly

Tengram's OpenSSL (3.5.7, static, built by `scripts/build-deps.sh`) uses OpenSSL's own ARM assembly
instead of the C code. On the phone that is 2 to 4.5 times the speed. It took two things: another
Configure target, and one patch so the library starts on BB10.

## 1. The Configure target: `linux-armv4`

The first builds used `linux-generic32`, which is C only. The target is now `linux-armv4`
(`OPENSSL_CONFIGURE_FLAGS` in `scripts/build-deps.sh`):

```
linux-armv4 no-shared no-tests no-dso no-engine no-async -DNO_SYSLOG -O2 -ffunction-sections -fdata-sections
```

- `linux-armv4` is the target that turns the assembly on. Configure runs the perlasm scripts
  (`crypto/*/asm/*.pl`, scheme `linux32`, ELF output) and defines `AES_ASM`, `BSAES_ASM`, `SHA1_ASM`,
  `SHA256_ASM`, `SHA512_ASM`, `GHASH_ASM`, `POLY1305_ASM`, `KECCAK1600_ASM`, `ECP_NISTZ256_ASM`,
  `OPENSSL_BN_ASM_MONT`, `OPENSSL_BN_ASM_GF2m` and `OPENSSL_CPUID_OBJ`. QNX has no target of its own
  with assembly; the Linux one works because the output is plain ELF for the same ABI.
- `no-dso no-engine no-async`: TDLib needs none of them.
- `-DNO_SYSLOG`: the SDK has no `<syslog.h>`.
- `-O2`: the target's default is `-O3`; ours comes after it on the command line and wins.
- `-ffunction-sections -fdata-sections`: for `--gc-sections` when the engine is linked.

The compiler is `bb10-clang`, with the QNX binutils' `ar` and `ranlib`, in the `tengram-build` Docker
image (`docker/Dockerfile` installs the full perl that Configure and perlasm need). The build makes
`build_generated` first, then only `libcrypto.a` and `libssl.a`: `make build_libs` also builds
`apps/`, which needs `syslog.h`.

## 2. The patch: `patches/openssl/0001-qnx-armcap.patch`

With the assembly, OpenSSL has to know at start which CPU extensions exist. On ARM without
`getauxval` it finds out in `OPENSSL_cpuid_setup` (`crypto/armcap.c`) by running one instruction of
each extension and catching SIGILL.

**On BB10 those probes leave the thread without an FPU:** its next VFP instruction dies with SIGFPE
(`FPE_NOFPU`).

The patch adds a `__QNX__` branch before the probes that sets the mask and returns:

```c
OPENSSL_armcap_P = ARMV7_NEON;
return;
```

It is what OpenSSL already does for 32-bit iOS, a few lines below. The mask is safe to fix: every
BB10 phone's CPU has NEON and none has the ARMv8 crypto extensions.

`build-deps.sh` applies every `patches/openssl/*.patch` to the extracted tarball, and the patches'
hash is in the build stamp, so a changed patch rebuilds OpenSSL.

## What runs

With the mask at `ARMV7_NEON`, the NEON paths are taken:

| Algorithm | Code |
|---|---|
| AES (CTR, CBC decrypt, XTS) | `bsaes-armv7.S`, bit-sliced NEON |
| AES, single blocks and key setup | `aes-armv4.S` |
| SHA-1, SHA-256, SHA-512 | `sha*-armv4*.S`, the `_neon` entries |
| Bignum (RSA, DH) | `armv4-mont.S` (`bn_mul8x_mont_neon`), `armv4-gf2m.S` |
| GCM | `ghash-armv4.S` (`gcm_ghash_neon`) |
| ChaCha20, Poly1305 | `chacha-armv4.S`, `poly1305-armv4.S` |
| SHA-3 | `keccak1600-armv4.S` |
| P-256 | `ecp_nistz256-armv4.S` |

`aesv8-armx.S` and `ghashv8-armx.S` (the ARMv8 extensions) are compiled in and never taken.

## Measured on the phone

`cryptobench`, the C build against this one (2026-11-16; the tool is not in the repo):

| | C | Assembly |
|---|---|---|
| AES-256-CTR | 18 MB/s | 59 MB/s |
| SHA-256 | 41 MB/s | 113 MB/s |
| SHA-512 | 28 MB/s | 63 MB/s |
| 2048-bit modexp | 13 per second | 58 per second |

## Checking a build

The NEON code is in the library when this prints symbols:

```sh
nm build/prefix/lib/libcrypto.a | grep -E 'sha256_block_data_order_neon|bn_mul8x_mont_neon|ossl_bsaes_ctr32'
```

`armcap.c` reads the `OPENSSL_armcap` environment variable before the patch's branch, so
`OPENSSL_armcap=0` in the engine's environment runs the same binary on the ARM code without NEON.
For the C code, Configure with `linux-generic32`.

## Who links it

`build/prefix/lib/libcrypto.a` and `libssl.a` go into TDLight (`build-deps.sh`, the cross pass), the
engine (`engine/build.sh`) and the calls library (`scripts/build-calls.sh`).
