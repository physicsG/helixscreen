# Branching

Three long-lived branches. Everything else is a short feature branch off `main`
or, for a 1.1 fix, off `release/1.1`.

| Branch | Purpose | `RELEASE_CHANNEL` | A tag here publishes to |
|--------|---------|-------------------|-------------------------|
| `main` | The trunk. All development lands here. Versions `1.2.0-beta.N`. | `dev` | dev |
| `release/1.1` | The 1.1 line: the remaining 1.1 betas, then `1.1.0` and its fixes. | `beta`, `stable` from `1.1.0` | beta + dev, then stable |
| `release/1.0` | The 1.0 maintenance line. | `stable` | stable |

**Only one branch may hold the beta channel.** `release.yml`'s downgrade guard
refuses a tag whose version is below what a channel already serves, and `beta`
publishes to dev as well. While `release/1.1` ships `1.1.0-beta.N` on `beta`, the
trunk says `dev` and is not tagged at all, because a `1.2.0-beta.1` on dev would
make the guard refuse every later 1.1 beta. When `1.1.0` ships, `release/1.1`
moves to `stable` and the trunk goes back to `beta`.

**There is no development branch.** Work goes to `main` through a feature branch
and a merge, and `main` is what ships to the beta and dev channels.

## Which way fixes flow

**A 1.1 fix lands on `release/1.1` first and is cherry-picked up to `main`.** The
1.1 line is what users are about to run, so its fixes are gated and tagged there;
the port to `main` follows in a batch. Features and refactors go to `main` only.

**Fix on `main`, cherry-pick down to `release/1.0`** when the 1.0 fleet needs it.
Never a merge between lines in either direction: they have diverged by a release
and a merge would drag the whole delta across. Every fix that reaches a release
line also reaches `main`.

`release/1.0` receives only what the 1.0 fleet needs — a crash, a data-loss bug,
a printer that stops being detected. It does not receive features, refactors, or
dependency bumps. A fix that will not cherry-pick cleanly is a fix that needs
writing twice; that is the cost of a maintenance line and it is expected.

## The channel constraint

`RELEASE_CHANNEL` is a file at the repo root, read by `scripts/release-channel.sh`
and consumed by `.github/workflows/release.yml`. It is declared per branch rather
than derived from the tag string, because routing is a property of the branch: the
`dev` channel cannot be spelled in a version at all, and a plain `vX.Y.Z` tag cut
from the trunk would reach the stable fleet if the tag decided. The version string
carries precedence only, and `helix::version::Version` orders a `-beta.N` build
below the release it names.

**`main` must never declare `stable`.** It carries versions ahead of every released
line, so a tag there would publish over the stable manifest for every user, and
`release.yml`'s pre-upload downgrade guard cannot refuse it — a higher version is
a *forward* move. Cutting a new maintenance line and flipping the trunk's channel
belong to one change, never two.

Nothing about moving a branch publishes anything: `release.yml` triggers only on
`push: tags: v*`, and the R2 workflows are `workflow_dispatch` only. A release is
always a deliberate tag. See `RELEASE_PROCESS.md`.

## What CI covers

`build.yml`, `quality.yml` and `lint-xml.yml` run on pushes to `main` and
`release/**`, and on pull requests against either. `esp32-build.yml` runs on the
same branches except `release/1.0`, which predates the ESP32 port.

**A long-lived branch outside those patterns gets no CI at all.** Code merged
there compiles only on whatever a developer happens to build, and defects that
need a different toolchain (a firmware target, a 32-bit `long`, a platform
without `gethostname()`) accumulate invisibly until the branch reaches one that
CI watches. Keep feature branches short and open a PR, or accept that the first
honest build is the merge.

The same principle is why nothing in the tree is compiled by a source list that
no job builds: an unbuilt tree rots silently and needs a bespoke gate to notice
what a build would have caught for free.
