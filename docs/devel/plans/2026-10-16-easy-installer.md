# Easy Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Tauri desktop app (Windows, macOS, Linux) that installs, upgrades and removes HelixScreen on commercial printers and generic Klipper hosts for owners who never touch a shell.

**Architecture:** A Rust core crate holds every decision as pure, testable code (manifest parsing, version ranges, identification, the step runner) behind four transport traits (Moonraker HTTP, SSH, drives, downloads) with scriptable fakes. A thin Tauri crate exposes the runner to a Svelte UI. All printer knowledge lives in a signed JSON manifest, so a new printer is data, not code.

**Tech Stack:** Rust 1.80+, Tauri 2, tokio, serde/serde_json, reqwest (rustls), russh, mdns-sd, minisign-verify, sha2, sysinfo; Svelte 5 + Vite + TypeScript; GitHub Actions with tauri-action. One change in this repo: `install.sh --progress` (POSIX sh, bats).

**Spec:** `docs/devel/plans/2026-10-16-easy-installer-design.md`

## Global Constraints

- License GPL-3.0-or-later; SPDX header on every source file.
- New repo `prestonbrown/helix-installer`. Creating it is public: confirm with Preston before Task 2's `gh repo create`.
- Never format a drive. Only removable drives are offered; FAT32 required, else refuse with instructions.
- Never write an artifact whose sha256 is not in the signed manifest (or, for HelixScreen releases, in `releases.helixscreen.org/<channel>/manifest.json`).
- Never repack or host third-party firmware; download from each mod's own release URL.
- Anything identification cannot map to a recipe is refused with a doc link. No guessing.
- `ssh_run` and `helix_install` commands come only from the signed manifest, never from user input or network responses.
- Offline manifest cache is usable only with a valid signature and age under 30 days.
- Passwords are never written to disk, logs, run state or the support bundle.
- Upload over SSH uses `cat > path` on an exec channel: BusyBox targets (AD5M, K1) have no sftp-server.
- A recipe ships only with `verified: {by, date, firmware}` filled from a real hardware run.
- Real printers: ask Preston once before the first command against any of them; anything that moves, heats, flashes or changes a printer is confirmed every time. Read `memory/reference_test_devices_index.md` and the board's own memory before first ssh.
- Plain `make`, `cargo` (jobpool shims); never hand-pick `-j`.
- Comments state the constraint, never the history.

## Review Focus

1. **The laptop sleeps or Wi-Fi drops mid-install.** Expect: the printer-side job keeps running, and reopening the app resumes at the right step by reading the job log. Test: Task 11 (detached job survives channel drop) and Task 9 (resume from saved state).
2. **Two printers of the same model on the network, or one printer reachable on two IPs.** Expect: the picker lists each once, keyed by serial or MAC where available, and never runs a recipe against a different host than the one the user chose. Test: Task 10 (dedup) and Task 9 (host pinned in run state, re-identify after point of no return).
3. **The user picks the wrong USB drive, or a drive whose label looks like a system disk.** Expect: non-removable and system disks are never listed; a non-FAT32 or too-small stick is refused before any byte is written. Test: Task 13.
4. **A firmware version string the manifest has never seen** (`3.1.10`, `V1.3.3.46`, `5.1.7-beta`). Expect: leading `V` and suffixes are handled by the comparator's documented grammar; anything unparseable refuses instead of matching. Test: Task 3.
5. **The default login is wrong (changed password, newer firmware).** Expect: the app tries the recipe's list once each, then asks the user, never locks them out with repeated attempts, and never logs the password. Test: Task 11 and Task 15.

---

## Milestone A: this repo

### Task 1: `install.sh --progress`

**Files:**
- Modify: `scripts/lib/installer/common.sh` (`step`, `step_done`, `step_fail`, `step_skip`, `_step_close`)
- Modify: `scripts/lib/installer/main.sh` (argument parsing near `--dry-run`; `usage`; emit platform and final result)
- Test: `tests/shell/test_installer_progress.bats`
- Docs: `docs/devel/INSTALLER.md` (new "Machine-readable progress" section)

**Interfaces:**
- Produces: with `--progress`, stdout carries one line per event, tab-separated, prefix `HELIX-PROGRESS`:
  - `HELIX-PROGRESS\tplatform\t<platform>` once, after detection
  - `HELIX-PROGRESS\tstep\t<n>\t<total>\t<status>\t<title>\t<detail>` where status is `start|ok|fail|skip`
  - `HELIX-PROGRESS\tresult\tok` or `HELIX-PROGRESS\tresult\tfail\t<reason>` as the last line
  - Tabs and newlines inside title/detail become single spaces.
- Human output is unchanged (still on stderr).

- [x] **Step 1: Write failing bats tests** covering: `--progress` emits exactly one `platform` line; every `step ... start` is followed by exactly one terminal status for the same `n`; a forced failure (existing test hooks used by `test_install_e2e.bats`) ends with `result fail` and a reason; without `--progress` stdout has no `HELIX-PROGRESS` line; a title containing a tab is flattened.
- [x] **Step 2: Run** `make test-shell` (or `bats tests/shell/test_installer_progress.bats`); expect FAIL.
- [x] **Step 3: Implement** a single `_progress_emit` helper in `common.sh` called from the step functions and `main.sh`; gate on `PROGRESS=1`.
- [x] **Step 4: Run** the new file plus `test_install_e2e.bats` and `test_installer_step_reachability_gate.bats`; expect PASS.
- [x] **Step 5: Commit** with `git commit -- <paths>` (shared main tree rules), then `make full-test-run` once at the end of the milestone.

---

## Milestone B: the engine (new repo, no hardware)

### Task 2: Repo scaffold and CI

**Files:**
- Create: `Cargo.toml` (workspace: `crates/core`, `crates/app`), `crates/core/src/lib.rs`, `crates/app/` (Tauri 2 template), `ui/` (Svelte 5 + Vite + TS), `LICENSE` (GPL-3.0), `README.md`, `.github/workflows/ci.yml`, `rust-toolchain.toml`
- Test: `crates/core/tests/smoke.rs`

**Interfaces:**
- Produces: `helix_installer_core` crate with no Tauri dependency; `helix_installer_app` depends on it.

- [x] **Step 1:** Confirm repo creation with Preston, then `gh repo create prestonbrown/helix-installer --public`.
- [x] **Step 2:** Scaffold with `cargo create-tauri-app` (Svelte + TS), move Rust into `crates/app`, add `crates/core`.
- [x] **Step 3:** CI matrix `ubuntu-latest, macos-latest, windows-latest`: `cargo fmt --check`, `cargo clippy -D warnings`, `cargo test --workspace`, `npm ci && npm run check` in `ui/`. Linux job installs `libwebkit2gtk-4.1-dev`.
- [x] **Step 4:** Smoke test asserts the core crate links; push; CI green on all three.
- [x] **Step 5:** Commit.

### Task 3: Facts and firmware version ranges

**Files:**
- Create: `crates/core/src/facts.rs`, `crates/core/src/version.rs`
- Test: `crates/core/tests/version.rs`

**Interfaces:**
- Produces:
  - `pub type Facts = BTreeMap<String, serde_json::Value>;`
  - `pub struct FwVersion(Vec<u32>)` with `pub fn parse(s: &str) -> Option<FwVersion>`: accepts dotted numerics, strips a leading `v`/`V`, ignores a `-suffix` (`5.1.7-beta` parses as 5.1.7). Anything else is `None`.
  - `pub struct VersionRange` with `pub fn parse(s: &str) -> Result<VersionRange, RangeError>` and `pub fn matches(&self, v: &FwVersion) -> bool`. Grammar: alternatives split by `||`; each alternative is space-separated comparators ANDed; comparator is `>=|<=|>|<|=` plus a version, or a bare version meaning `=`; a final `x` component is a wildcard (`5.1.x`). Comparison pads the shorter version with zeros.
- [x] **Step 1: Failing tests:** `1.3.3.5 < 1.3.3.46`; `3.1.10 > 3.1.9`; `V1.2.3 == 1.2.3`; `5.1.7-beta` matches `5.1.x`; `">=1.1.7 <=1.2.3 || 3.0.3 || 3.0.9 || 3.1.0"` matches `1.2.0` and `3.0.9`, not `3.0.4` or `3.1.4`; `"2.6.5"` padded equals `2.6.5.0`; `parse("abc")`, `parse("")`, `parse("1..2")` are `None`; `VersionRange::parse(">= 1.2")` (space after operator) is an error, not a silent match-all.
- [x] **Steps 2-4:** run `cargo test -p helix_installer_core --test version` (FAIL), implement, run (PASS).
- [x] **Step 5:** Commit.

### Task 4: Manifest model and signature

**Files:**
- Create: `crates/core/src/manifest.rs`, `crates/core/src/manifest_sig.rs`, `crates/core/tests/fixtures/manifest_min.json`, `crates/core/tests/fixtures/test_key.pub`, `crates/core/tests/fixtures/test_key.sec` (test-only key)
- Test: `crates/core/tests/manifest.rs`

**Interfaces:**
- Consumes: `VersionRange`, `Facts`.
- Produces:
  - `pub struct Manifest { pub manifest_version: u32, pub min_app_version: String, pub printers: Vec<Printer>, pub artifacts: BTreeMap<String, Artifact>, pub recipes: BTreeMap<String, Vec<Step>> }`
  - `pub struct Printer { pub id, pub name, pub photo, pub identify: Vec<IdentifyRule>, pub recipes: Vec<RecipeChoice>, pub restore: String, pub fallback: Fallback, pub verified: Option<Verified> }`
  - `pub struct RecipeChoice { pub when: BTreeMap<String, Cond>, pub recipe: String }`, and `IdentifyRule` is the same `BTreeMap<String, Cond>`.
  - `pub enum Cond { Exact(String), Contains{contains: String}, Range(VersionRange) }`: a plain string is `Exact`, except under a key ending `_version`, where it parses as a `VersionRange`; `{"contains": "x"}` matches a list fact that has `x` as an element.
  - `pub struct Artifact { pub url: String, pub sha256: String, pub filename: Option<String> }`
  - `pub enum Step` (serde tag `type`): `Info{text, image}`, `UserAction{text, image, verify: Option<Probe>}`, `Ask{fact, question, choices: Vec<Choice>}`, `Probe(Probe)`, `Require{fact, range, doc}`, `Download{artifact}`, `WriteStick{artifacts: Vec<String>, rename: BTreeMap<String,String>}`, `WaitFor{target: WaitTarget, timeout_s, message, help}`, `SshLogin{logins: Vec<Login>, ask_if_failed: bool}`, `SshRun{command, timeout_s}`, `MoonrakerGcode{script}`, `MoonrakerUpdate{name}`, `HelixInstall{platform: PlatformSel, update: bool, dir: Option<String>}`, every variant also carrying `#[serde(default)] commits: bool` (marks the point of no return).
  - `pub fn verify_and_parse(bytes: &[u8], sig: &str, pubkey: &str, app_version: &str) -> Result<Manifest, ManifestError>`; errors: `BadSignature`, `TooNew{min_app_version}`, `Malformed(String)`.
  - `pub struct ManifestCache { dir: PathBuf }` with `load(now) -> Option<Manifest>` returning only a validly signed manifest fetched under 30 days ago, and `store(bytes, sig, now)`.
- [x] **Step 1: Failing tests:** good signature parses; one flipped byte fails `BadSignature`; `min_app_version` above the running version fails `TooNew`; an unknown step `type` fails `Malformed` (never ignored); cache returns `None` at 30 days + 1 s and `Some` at 29 days; a cached file with a broken signature returns `None`.
- [x] **Steps 2-5:** fail, implement, pass, commit.

### Task 5: Identification

**Files:**
- Create: `crates/core/src/identify.rs`
- Test: `crates/core/tests/identify.rs`, fixtures `crates/core/tests/fixtures/probes/*.json`

**Interfaces:**
- Consumes: `Manifest`, `Facts`, `VersionRange`.
- Produces:
  - `pub enum StartState { Stock, Modded(String), Helix(String) }` with `pub fn as_fact(&self) -> String` (`"stock"`, `"modded:<mod>"`, `"helix"`).
  - `pub fn start_state(f: &Facts) -> StartState`: `helix_installed=true` wins (version from `helix_version`), else `has_moonraker=true` gives `Modded("moonraker")`, else `Stock`. Which mod is present is not core logic: recipes tell mods apart with `when` conditions on `klipper_objects` and `services` (Task 10), for example ZMOD by its Klipper device names.
  - A printer nothing on the network can see (a rooted-stock K1 has no Moonraker) is identified from the fact `user_model`, set when the user picks it from the model grid.
  - `pub enum Identified { Recipe{printer_id: String, state: StartState, recipe_id: String}, Ambiguous{candidates: Vec<String>}, Refuse{printer_id: Option<String>, doc: String, reason: String} }`
  - `pub fn identify(m: &Manifest, f: &Facts) -> Identified`. A printer matches when every key/value of any one of its `identify` rules equals the fact. Zero printers match: `Refuse` with the generic doc. Two or more: `Ambiguous` (UI asks). Then the first `RecipeChoice` whose every `when` key matches wins; a version key whose fact is missing or unparseable does not match. No choice matches: the printer's `fallback`.
- [x] **Step 1: Failing tests** with probe fixtures for: K1 rooted stock (no Moonraker), K1 with Simple AF, K2 stock, Q2, Max 4, MainsailOS Pi 5, AD5X stock 3.1.4 (refuse in v1), AD5X with ZMOD, AD5M stock 3.1.5, any printer with `helix_installed` (upgrade recipe). Plus: a fact matching two printers gives `Ambiguous`; a `stock_version` of `"unknown"` refuses; `user_model=k1` with no network facts gives the K1 stock recipe; a `contains` rule matches only list facts.
- [x] **Steps 2-5:** fail, implement, pass, commit.

### Task 6: Manifest lint

**Files:**
- Create: `crates/core/src/lint.rs`, `crates/core/src/bin/manifest-lint.rs`
- Test: `crates/core/tests/lint.rs`

**Interfaces:**
- Produces: `pub fn lint(m: &Manifest, asset_root: &Path) -> Vec<LintError>`; binary exits 1 on any error and prints one line each.
- Rules: every artifact has a 64-hex sha256 and an https URL; every printer has `fallback.refuse == true` with a doc URL, and a `restore` naming an existing recipe; every recipe and artifact referenced exists; every `image`/`photo` file exists under `asset_root`; every recipe ends in a step that leaves the run done or refused (last step is not `Info`-only padding: allowed terminals are `WaitFor` on a HelixScreen service, `HelixInstall`, `MoonrakerUpdate`, or `Require`); each printer's `RecipeChoice` list has no two choices that can both match the same facts (checked over the cross product of exact values and range boundary versions); at most one step per recipe has `commits: true`; a printer without `verified` fails lint unless `--allow-unverified`.
- [x] **Steps 1-5:** one failing test per rule (a manifest violating only that rule), implement, pass, commit.

### Task 7: Transport traits and fakes

**Files:**
- Create: `crates/core/src/transport/mod.rs`, `transport/fake.rs`
- Test: `crates/core/tests/fakes.rs`

**Interfaces:**
- Produces (all `#[async_trait]`, `Send + Sync`):
  - `trait Moonraker { async fn printer_info(&self, host: &Host) -> Result<Value>; async fn system_info(&self, host) -> Result<Value>; async fn update_status(&self, host) -> Result<Value>; async fn gcode(&self, host, script: &str) -> Result<()>; async fn update_upgrade(&self, host, name: &str) -> Result<()>; }`
  - `trait Ssh { async fn connect(&self, host: &Host, login: &Login) -> Result<Box<dyn SshSession>, SshError>; }` with `SshError::{Unreachable, AuthFailed, Other}`
  - `trait SshSession { async fn exec(&mut self, cmd: &str, stdin: Option<&[u8]>, timeout: Duration) -> Result<ExecOutput>; async fn upload(&mut self, local: &Path, remote: &str) -> Result<()>; async fn spawn_detached(&mut self, cmd: &str, sudo_password: Option<&str>) -> Result<RemoteJob>; async fn read_job(&mut self, job: &RemoteJob, from_offset: u64) -> Result<JobChunk>; }`; `RemoteJob { log_path: String }`, `JobChunk { text: String, next_offset: u64, exit: Option<i32> }`
  - `trait Drives { fn list_removable(&self) -> Result<Vec<Drive>>; async fn copy_to(&self, d: &Drive, src: &Path, name: &str) -> Result<()>; async fn eject(&self, d: &Drive) -> Result<()>; }`; `Drive { id, label, mount: PathBuf, fs: String, size_bytes, free_bytes, removable: bool, system: bool }`
  - `trait Fetch { async fn get(&self, url: &str, dest: &Path, progress: &dyn Fn(u64, Option<u64>)) -> Result<()>; async fn get_text(&self, url: &str) -> Result<String>; }`
  - `pub struct Env { pub moonraker: Arc<dyn Moonraker>, pub ssh: Arc<dyn Ssh>, pub drives: Arc<dyn Drives>, pub fetch: Arc<dyn Fetch>, pub clock: Arc<dyn Clock> }`
  - Fakes: `FakeMoonraker` (per-host canned JSON, records gcode calls), `FakeSsh` (per-login accept/reject, a scripted command table, a `drop_after(n)` knob that kills the session mid-job), `FakeDrives`, `FakeFetch` (serves bytes from a map), `FakeClock`.
- [x] **Steps 1-5:** tests that each fake records and replays what later tasks need; commit.

### Task 8: Download and cache

**Files:**
- Create: `crates/core/src/steps/download.rs`, `crates/core/src/cache.rs`
- Test: `crates/core/tests/download.rs`

**Interfaces:**
- Produces: `pub async fn fetch_artifact(env: &Env, a: &Artifact, cache_dir: &Path) -> Result<PathBuf, DownloadError>`; `DownloadError::{Network, HashMismatch{expected, got}}`. A cached file is re-hashed before reuse; a mismatching partial or cached file is deleted.
- [x] **Step 1: Failing tests:** wrong bytes give `HashMismatch` and leave no file; a correct cached file is reused without a fetch; a corrupted cached file is refetched.
- [x] **Steps 2-5:** fail, implement, pass, commit.

### Task 9: Runner

**Files:**
- Create: `crates/core/src/runner.rs`, `crates/core/src/run_state.rs`, `crates/core/src/steps/{info.rs,ask.rs,require.rs,probe.rs,wait_for.rs,moonraker.rs}`
- Test: `crates/core/tests/runner.rs`

**Interfaces:**
- Consumes: `Env`, `Manifest`, `Step`, `Facts`, `fetch_artifact`.
- Produces:
  - `pub struct RunState { pub run_id: String, pub printer_id: String, pub recipe_id: String, pub host: Host, pub step_index: usize, pub facts: Facts, pub committed: bool, pub job: Option<RemoteJob>, pub log: Vec<LogEntry> }` (serde; no secrets)
  - `pub enum Prompt { Info{text, image}, UserAction{text, image}, Ask{question, choices}, Login{user_hint: Option<String>}, ChooseDrive{drives: Vec<Drive>, needed_bytes: u64}, Ambiguous{candidates} }`
  - `pub enum Input { Next, Choice(String), Login(Login), Drive(String), Cancel }`
  - `pub enum Event { NeedInput(Prompt), Progress{step: usize, total: usize, title: String, detail: Option<String>, fraction: Option<f32>}, Done, Refused{doc: String, reason: String}, Failed{committed: bool, step: usize, reason: String, help: Option<String>} }`
  - `pub struct Runner` with `pub fn new(env, manifest, state) -> Runner`, `pub async fn advance(&mut self, input: Option<Input>) -> Event`, `pub fn save(&self, dir: &Path) -> io::Result<()>`, `pub fn load(dir: &Path, run_id: &str) -> io::Result<RunState>`, `pub fn list_unfinished(dir: &Path) -> Vec<RunState>`.
  - After a `commits: true` step completes, every subsequent `Probe` result for `model` or `serial` is compared with the facts recorded before the commit; a difference is `Failed{committed: true}` with "this is not the printer the install started on".
- [x] **Step 1: Failing tests:** a 3-step recipe of `Info`, `Require` (pass) and `WaitFor` against fakes reaches `Done`; a failing `Require` before any commit gives `Refused` with the doc URL; a `WaitFor` timeout after a commit gives `Failed{committed: true}` with the step's help text; state saved after step 2 and loaded into a new `Runner` resumes at step 3; `list_unfinished` returns it; the saved JSON contains no field named or containing `password`; a model change after commit fails as above.
- [x] **Steps 2-5:** fail, implement, pass, commit.

### Task 10: Discovery

**Files:**
- Create: `crates/core/src/discovery/{mod.rs,mdns.rs,sweep.rs,flashforge.rs}`, real `Moonraker` impl in `crates/core/src/transport/moonraker_http.rs`
- Test: `crates/core/tests/discovery.rs` (flashforge parser + dedup are pure), `moonraker_http` against a local `axum` test server

**Interfaces:**
- Produces:
  - `pub struct Candidate { pub host: Host, pub facts: Facts, pub key: String }` where `key` is serial, else MAC, else host.
  - `pub async fn discover(env: &Env, timeout: Duration) -> Vec<Candidate>`: mDNS `_moonraker._tcp`, a /24 sweep of each local IPv4 interface on port 7125 (64 concurrent connects, 300 ms each), and FlashForge UDP (broadcast to 48899, listen on 18007). Results are merged by `key`.
  - `pub fn parse_flashforge_reply(bytes: &[u8]) -> Option<Facts>` setting `flashforge_udp.model`, `serial`.
  - Facts from Moonraker: `has_moonraker`, `klipper_hostname`, `cpu_arch`, `os_distribution`, `klipper_objects` (list, from `printer.objects.list`), `services` (list, `machine.system_info` `available_services`), `helix_installed`/`helix_version` (from the update_manager entry `helixscreen`, else `helixscreen` in `services`).
  - `pub fn manual_host(ip: &str) -> Result<Host>` for typing an IP.
- [x] **Step 1: Failing tests:** parse a captured 276-byte FlashForge reply (fixture from `GhostTypes/ff-5mp-api-py` test data or our own capture in Task 18); a truncated reply is `None`; one printer seen by mDNS and by the sweep on the same IP, and a printer seen on two IPs with the same serial, each yield one candidate; Moonraker 404 on update_manager still yields `has_moonraker=true`.
- [x] **Steps 2-5:** fail, implement, pass, commit.

### Task 11: SSH transport

**Files:**
- Create: `crates/core/src/transport/ssh_russh.rs`
- Test: `crates/core/tests/ssh.rs` using an in-process `russh::server` with a scripted shell

**Interfaces:**
- Implements `Ssh`/`SshSession` from Task 7.
- `upload` streams the file into `cat > '<remote>.part' && mv '<remote>.part' '<remote>'`, then checks size with `wc -c`.
- `spawn_detached(cmd, sudo)` runs `setsid sh -c '<cmd>; echo "HELIX-EXIT $?"' > <log> 2>&1 < /dev/null &` (falls back to `nohup` when `setsid` is missing) under `/tmp/helix-installer-<id>.log`; with `sudo` it prefixes `sudo -S -p ''` and writes the password plus newline to stdin. If sudo answers "you must have a tty", it retries once with a PTY.
- `read_job` reads from the byte offset with `tail -c +<n>`; `exit` is set when the `HELIX-EXIT` line arrives.
- Login policy lives in the `SshLogin` step (Task 15), not here: one `connect` per credential.
- [x] **Step 1: Failing tests:** upload of 5 MB round-trips byte-exact; a dropped connection during a job, then a new session's `read_job`, still sees the `HELIX-EXIT 0` line; sudo password goes through stdin (the test server's command log never contains it); a wrong password returns `AuthFailed` (not `Other`).
- [x] **Steps 2-5:** fail, implement, pass, commit.

### Task 12: `helix_install`, `ssh_run`, `moonraker_*` steps

**Files:**
- Create: `crates/core/src/steps/{helix_install.rs,ssh_run.rs,progress.rs}`
- Test: `crates/core/tests/helix_install.rs`

**Interfaces:**
- Consumes: Task 1's `HELIX-PROGRESS` format, `SshSession`, `Fetch`.
- Produces:
  - `pub fn parse_progress_line(line: &str) -> Option<ProgressEvent>`; `ProgressEvent::{Platform(String), Step{n, total, status, title, detail}, Result{ok: bool, reason: Option<String>}}`.
  - `PlatformSel` is either a fixed platform (`"k1"`) or `{ "from_arch": {"aarch64": "pi", "armv7l": "pi32"} }` resolved from the `cpu_arch` fact.
  - `helix_install` fetches `https://releases.helixscreen.org/stable/manifest.json`, takes `assets.<platform>.zip_url` and `zip_sha256`, downloads the zip and `https://releases.helixscreen.org/install.sh` on the PC, uploads both to a writable dir the recipe names (default `/tmp`; AD5M recipes use `/data`), runs `sh <dir>/install.sh --local <dir>/helixscreen-<platform>.zip --progress [--update]` detached, and maps progress lines to `Event::Progress`. Success requires `result ok` and the installer's reported platform equal to the chosen one.
- [x] **Step 1: Failing tests:** progress parser round-trips every line form and rejects garbage; platform mismatch (`pi` chosen, installer says `pi32`) fails; `result fail` surfaces the reason; a job that dies without a `result` line after its log stops growing for the step timeout fails as a timeout, not success.
- [x] **Steps 2-5:** fail, implement, pass, commit.

### Task 13: Drives and `write_stick`

**Files:**
- Create: `crates/core/src/transport/drives_sys.rs`, `crates/core/src/steps/write_stick.rs`
- Test: `crates/core/tests/write_stick.rs` (fake drives); a manual check per OS listed in the step below

**Interfaces:**
- `drives_sys` uses `sysinfo::Disks` for mount, filesystem, size and the removable flag, and marks `system: true` for the disk holding the OS root, the user's home or the app's own directory. Platform eject: macOS `diskutil eject`, Linux `udisksctl unmount` + `power-off`, Windows `ShellExecute` of the Eject verb via PowerShell `(New-Object -comObject Shell.Application).Namespace(17).ParseName('<X:>').InvokeVerb('Eject')`. If `sysinfo`'s removable flag proves wrong on a platform, port the matching check from Raspberry Pi Imager's `drivelist` (Apache-2.0) with attribution.
- `write_stick` lists only `removable && !system` drives, refuses non-FAT32 (`vfat`, `msdos`, `FAT32` spellings) and too little free space, copies each artifact under its `rename` name, verifies sha256 of the copied file, and ejects.
- [x] **Step 1: Failing tests:** a non-removable drive and a removable drive marked `system` never appear in `ChooseDrive`; exFAT and NTFS sticks refuse with the "reformat as FAT32" help; a stick 1 byte too small refuses before copying; the copy is verified by hash; the Pro rename applies.
- [ ] **Step 2-4:** fail, implement, pass. Manual: run the `drives-probe` debug binary on Windows, macOS and Linux with a stick inserted; record output in the PR.
- [x] **Step 5:** Commit.

### Task 14: Support bundle

**Files:**
- Create: `crates/core/src/bundle.rs`
- Test: `crates/core/tests/bundle.rs`

**Interfaces:**
- Produces: `pub fn support_bundle(state: &RunState, job_logs: &[String], mask: bool) -> String` (plain text). Strips anything matching a login password seen in the run, masks IPv4/IPv6, MACs and the `serial` fact unless `mask` is false.
- [x] **Steps 1-5:** tests with a log containing a password, an IP, a MAC and a serial; fail, implement, pass, commit.

### Task 15: `ssh_login` step

**Files:**
- Create: `crates/core/src/steps/ssh_login.rs`
- Test: `crates/core/tests/ssh_login.rs`

**Interfaces:**
- Tries each `Login` in order once; on `AuthFailed` for all, emits `Prompt::Login` if `ask_if_failed`, else `Failed`. A user-supplied login gets three tries, then `Failed` with the recipe's help. Sets fact `ssh_user` and `needs_sudo` (`id -u` is not 0). The password is held in memory on the `Runner` only.
- [x] **Steps 1-5:** tests: first default wins; second default wins; both fail then prompt; three wrong user logins stop; `needs_sudo` set for a non-root user; fail, implement, pass, commit.

### Task 16: UI

**Files:**
- Create: `crates/app/src/commands.rs` (Tauri commands), `ui/src/routes/{Picker.svelte,Run.svelte,Resume.svelte,Recovery.svelte}`, `ui/src/lib/api.ts`
- Test: `ui/src/lib/api.test.ts` (vitest, mocked `invoke`); `crates/app/tests/commands.rs`

**Interfaces:**
- Tauri commands: `discover() -> Vec<CandidateView>`, `start_run(candidate_key, printer_id?) -> RunId`, `advance(run_id, input?) -> Event`, `unfinished_runs() -> Vec<RunSummary>`, `export_bundle(run_id, mask) -> String`, `add_manual_host(ip) -> CandidateView`.
- Screens: printer picker (discovered printers plus "My printer isn't listed", which shows the model grid and an IP field); one screen per `Prompt`; a progress screen; recovery mode when `Failed{committed: true}` (names the state, the next action, the support bundle button, never "something went wrong"); a resume banner on launch when `unfinished_runs` is non-empty.
- [x] **Steps 1-5:** api tests for each command's error mapping; a full run against fakes driven from `crates/app/tests`; commit. Screenshot each screen at 1280x800 and 1024x640 for review.

### Task 17: Record mode

**Files:**
- Create: `crates/core/src/record.rs`, hidden UI toggle (5 clicks on the version label)
- Test: `crates/core/tests/record.rs`

**Interfaces:**
- `pub fn record(candidate: &Candidate, probes: &[(String, Value)]) -> String` writes JSON identical in shape to the Task 5 fixtures, with the same masking as the support bundle except `model`, firmware version facts and `cpu_arch`.
- [x] **Steps 1-5:** a recorded fixture round-trips through `identify`; commit.

---

## Milestone C: recipes and hardware

Each task: write the manifest entry and screenshots, run the lint, run the recipe end to end against fakes in CI, then one supervised hardware run (ask Preston first; confirm every write) that also records the probe fixture and fills `verified`. A recipe with no hardware run stays out of the published manifest.

### Task 18: Manifest repo layout and signing

**Files:**
- Create: `manifest/manifest.json`, `manifest/img/`, `.github/workflows/manifest.yml`
- Interfaces: CI runs `manifest-lint`, then signs with the key in the `MANIFEST_SIGNING_KEY` secret (environment with required reviewer: Preston), and uploads `manifest.json` + `.minisig` + `img/` to `releases.helixscreen.org/installer/`.
- [x] Steps: lint gate on PRs, sign-and-publish only on `main`, generate the production key offline and commit only the public key into `crates/core/src/keys.rs`; commit.

### Task 19: Generic Klipper host and QIDI recipes

- Recipes per the spec's v1 table. Generic: `SshLogin{logins: [], ask_if_failed: true}`, `HelixInstall{platform: from_arch, update}`. QIDI: logins `qidi/qiditech`, then `mks/makerbase`; `Info` about QIDI Studio box sync and cloud before the commit.
- Hardware: pi5 and pi3 running MainsailOS (sudo over stdin, then PTY fallback). Q2/Max 4/Plus 5: community-verified via a tester's recording.
- [ ] Steps: fixtures from pi5/pi3 into Task 5 tests; restore recipe (`install.sh --uninstall`) run on pi5; commit.

### Task 20: K2 recipe

- `UserAction` root toggle with screenshot, `verify` = TCP 22 open; logins `root/creality_2024`, then ask with the hint "the password shown on the printer's root screen".
- Hardware: our K2 (password fixed or generated; Moonraker before root).
- [ ] Steps: record, run install, run restore, fill `verified`; commit.

### Task 21: K1 recipe

- Spec v1 table K1 row. `ssh_run` for Simple AF: `git config --global http.sslVerify false; git clone https://github.com/pellcorp/creality.git /usr/data/pellcorp && sh /usr/data/pellcorp/k1/installer.sh --install <probe>`; `require` that no Helper Script, Creality `fluidd.sh`/`mainsail.sh` or real `/opt` exists (Simple AF refuses those).
- Before shipping: message pellcorp about the app driving the installer.
- Hardware: our K1 (probe flag for stock, run time, GrumpyScreen replaced).
- [ ] Steps: record, run, restore (HelixScreen uninstall to GrumpyScreen; link Creality recovery for full stock), fill `verified`; commit.

### Task 22: AD5X and AD5M recipes

- AD5X: stock in ZMOD's window, ZMOD artifact, `WriteStick`, flash `UserAction`, `WaitFor` Moonraker 40 min with "remove the stick afterwards", `MoonrakerGcode{"DISPLAY_OFF HELIX=1"}`, `WaitFor` HelixScreen. Helix state: `MoonrakerUpdate{"helixscreen"}`.
- AD5M: stock 2.6.5 to 5.1.x minus `-Factory`-needing versions, upstream Forge-X artifact with Pro rename, flash, `WaitFor` Moonraker, `SshLogin root/root`, `HelixInstall{platform: "ad5m", dir: "/data"}`. ZMOD-modded AD5M: the macro step.
- Before shipping: message ghzserg about the app driving ZMOD.
- Hardware: our AD5X (plug in first). AD5M: tester recording.
- [ ] Steps: record, run, restore, fill `verified`; commit.

---

## Milestone D: release

### Task 23: Signed builds and updater

- `tauri-action` release workflow on tag: `.msi`/`.exe`, notarized `.dmg`, AppImage and `.deb`. Secrets: Apple Developer ID cert + notarization credentials, Azure Trusted Signing account, Tauri updater private key. Updater endpoint on `releases.helixscreen.org/installer/app/latest.json`.
- Preston buys the Apple Developer membership and sets up Azure Trusted Signing; the plan cannot do this for him.
- [ ] Steps: dry-run release to a draft; install each artifact on a clean VM per OS; confirm no SmartScreen/Gatekeeper block; commit.

### Task 24: Docs and download page

- helixscreen.org/installer page choosing the file by OS. `docs/user/INSTALL.md` gets an "Easy installer" section first in Quick Start; each printer guide gets one line pointing at it.
- Per `docs/CLAUDE.md`, when the app ships delete this plan and the design spec, moving durable facts into the installer repo's README and `docs/devel/INSTALLER.md`.
- [x] Steps: write, review, commit.
