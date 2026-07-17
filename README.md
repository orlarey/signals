# signals

Standalone version of the Faust compiler's signal library: construction,
pretty-printing and visiting of signal expressions, the signal type system
(with interval computation), and signal analyses (recursiveness, sharing).

Built on [tlib](../tlib), the maximally-shared tree library. The Faust
compiler consumes this library — like tlib and interval — **by copy**: the
copy unit below is byte-identical to its `faust/compiler/` counterpart, and
`cp` is the whole import step.

## Lineages

This repository has two lineages, one per compiler branch:

- **`master-dev`** (this branch) — the current master-dev compiler, restructured
  at equal features (no on-demand). Simpler: no `Dependencies/`, no
  `DirectedGraph/`.
- **`fir18`** — the FIR18 compiler lineage, with on-demand and the extra
  features (clock inference, FIR/IIR, the dependency graph). Carries
  `Dependencies/` and vendors `DirectedGraph/`.

The two lineages will merge here, in the library, when on-demand is folded
back into master-dev — a few thousand lines, rather than in the 280-file
compiler.

## Layout

| Directory | Role | Synced with |
| --- | --- | --- |
| `signals/` | copy unit — the library itself | `faust/compiler/signals/` |
| `tlib/` | vendored from its own repository | `../tlib/tlib/` |
| `interval/` | vendored from its own repository | `../interval/interval/` (branch faust-master-dev) |
| `FaustAlgebra/` | vendored from `../interval` | `../interval/FaustAlgebra/` |
| `tests/` | standalone unit tests | — |

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/signals-tests
```

## Host integration

The library is host-configurable through a few hooks with autonomous
defaults (`signals/sigs-config.hh`): error handling comes from tlib
(`tlib::setErrorHandler`), and the host can install its own real-number
printer (`sigs::setRealPrinter`). Symbol visibility is controlled by the
`SIGS_API` macro (`signals/sigs-export.hh`).

Standalone hosts initialize with `tlib::init()` then `sigs::init()`, once
per session. The Faust compiler does **not** call `sigs::init()`: it
performs the same writes itself (`global.cpp`), in its own order-sensitive
sequence, through reference members bound to the library state `sigs::g`.

## Sync workflow (the copy contract)

1. Modify and test here (`cmake --build build && ./build/signals-tests`).
2. Import into faust: `cp signals/* faust/compiler/signals/`. Byte-identical,
   no local patches on either side.
3. Rebuild faust and run the A/B protocol: 0 codegen diff, 0 return-code diff
   required.
4. After every merge on the faust side, check `git log -- compiler/signals`
   and port third-party commits here **before** the next sync — a `cp` would
   silently overwrite them.
