# `.github/ci/` — how qbm-redis tests itself

This directory exists because of one fact that is easy to rediscover the hard way:

> **qbm-redis cannot be configured on its own.**

```console
$ cmake -S . -B build
CMake Error at CMakeLists.txt:40 (message):
  [qbm-redis] this module cannot be configured on its own.
```

That `FATAL_ERROR` is deliberate — it fires before CMake reaches the first `qb_*` call, so the
symptom names the cause instead of reading like a missing `include()`.

## Why an installed qb does not fix it

`CMakeLists.txt` and `tests/CMakeLists.txt` call `qb_status_message()`, `qb_register_module()` and
`qb_register_module_test()`. Those live in **`qb/cmake/qbFunctions.cmake`** and are *development-time*
helpers: an installed qb ships `lib/cmake/qb/{qbConfig,qbConfigVersion,qbTargets}.cmake` plus the
`Find*.cmake` modules, and none of the `qb_*` commands. So

```console
$ cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/qb   # does NOT help
```

is the mistake the guard is there to name. There is no combination of `find_package(qb)` arguments
that makes a standalone configure work, and there is not meant to be — `find_package(qb)` is for
*consuming* qb, not for building a module's test tree.

## What does work

A minimal root that `add_subdirectory()`s a qb **source** tree first and this module second — which
is exactly what the (private) `qb-dev` superproject root does. That root is committed here:

    .github/ci/superbuild/CMakeLists.txt

It takes the two trees as cache variables instead of assuming a layout, and it is **byte-identical**
in `qbm-http`, `qbm-pgsql` and `qbm-redis` (nothing in it names a module). `qb-dev`'s
`dev/agent/verify.sh` asserts the three copies have not drifted.

### Reproduce the CI lane locally

```sh
git clone --depth 1 -b develop https://github.com/isndev/qb ../qb

cmake -S .github/ci/superbuild -B ../build-qbm-redis -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DQBM_CI_QB_DIR="$PWD/../qb" \
      -DQBM_CI_MODULE_DIR="$PWD"

# build this module's test binaries only (qb's own ~174 are qb's CI's business)
ctest --test-dir ../build-qbm-redis -N -L module:qbm-redis

cmake --build ../build-qbm-redis --parallel --target <the targets that listing named>
ctest --test-dir ../build-qbm-redis -L module:qbm-redis
```

Use the **branch of the same name**: qb, the three qbm modules and the superproject all carry
`main` (the released line) and `develop` (the next version) and move together.
`.github/workflows/tests.yml` resolves that automatically and proves the ref exists before checking
it out.

### Daemons

27 of the 42 tests are `REQUIRES live`: they need a reachable Redis at `tcp://localhost:6379`
(`REDIS_URI`). Nothing needs pre-start setup, so CI uses a plain `services:` container — unlike
qbm-pgsql, whose postgres needs a TLS certificate generated *before* the server starts and therefore
cannot be a service container.

```sh
docker run -d --name qb-redis -p 6379:6379 redis:7-alpine
export REDIS_URI='tcp://localhost:6379'
```

A wedged local Redis is the usual cause of a blocking-command test appearing to hang (`BLPOP` and
friends). Restart the server before suspecting the test.

## One trap before you make this a required check

`tests.yml` carries `paths:` filters — the same shape `doc-lint.yml` here already uses — so a
pull request that touches only Markdown does not run it. That is deliberate: a docs-only PR cannot
break a test, and the lane costs a full build of qbm-redis's binaries.

It has a consequence, and it is the classic one. If `tests` is ever added to this branch's
protection rules as a **required** status check, a docs-only PR will sit forever on
*"Expected — Waiting for status to be reported"*, because a filtered-out workflow reports nothing
at all rather than reporting success. GitHub's answer is a companion job with the same check name
that always runs and passes when the real one is filtered out; this repository does not have one
yet. Decide that before turning the check on, not after.

## What this lane covers, and what it does not

It answers **"does qbm-redis still work with qb?"**. It does not answer *"do all three modules still
work together?"* — a qb-side change lands with no push here, so nothing in this repository fires for
it. The superproject's `qbm-tests.yml` remains the only lane that sees qb and all three modules at
once.

`ctest` counts `Skipped` as passed and still prints `100% tests passed`, so a green here is only
worth something because the workflow asserts, separately: a registration floor taken before the
build (the module total **and** the integration tier on its own), a non-zero translation-unit count,
an executable on disk for every registered test, an **executed** floor, and `skipped == 0`. With no
reachable Redis, 27 tests would self-skip, `ctest` would exit 0 and print `100% tests passed` — that
outcome fails this lane. A floor may only ever go **up**.

Unlike qbm-http, nothing here is host-varying: 42 registered (13 unit + 2 system + 27 integration) on
macOS and 42 on `ubuntu-latest`, measured. qbm-http has to derive part of its floor from a capability
because `ubuntu-latest` cannot build its four HTTP/3 tests. If a capability gate is ever added to this
module, its floor has to become derived in the same way — a constant would then be red on a host that
is merely different, not broken.
