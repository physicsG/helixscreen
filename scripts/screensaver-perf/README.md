# Screensaver performance measurement

Scripts that measure what a screensaver costs on a real board: CPU per thread, presented
frames, and whether the printer's timing survives it (the load gate). They drive a deployed
app over ssh and `helix-screen ctl`, so they need a build with the control server.

## Settings

Every script sources `perf_env.sh`. Nothing about a device is written in the repo; export these:

| Variable | Meaning |
|---|---|
| `HELIX_PERF_HOST` | Device address (required) |
| `HELIX_PERF_SCRATCH` | Directory for results, logs, traces and copied binaries, outside the repo (required) |
| `HELIX_PERF_USER` | ssh user, default `pi` |
| `HELIX_PERF_PASSWORD` | ssh and sudo password; unset means key auth and passwordless sudo. Passed to `sshpass -e`, never on a command line |
| `HELIX_PERF_INSTALL` | Install root on the device, default `/home/pi/helixscreen` |
| `HELIX_PERF_CTL_SOCK` | Control socket on the device, default `/run/helixscreen/control.sock` |
| `HELIX_PERF_BUILD_TREE` | Tree `arm_measure.sh` builds with `make pi-docker` |
| `HELIX_PERF_PROBE` | Wake-up probe binary for `embedded_gate.sh` |

## The load gate

A run passes when the latency probe records **no wake-up of 20 ms or more and a p99 under
5 ms**, under a CPU load that stands in for klippy and moonraker. An arm passes only when
every run passes. On the Pi the probe is `cyclictest --laptop --policy=other -i 1000` beside
`stress-ng --cpu 2`; on BusyBox boards it is `wakeup_probe` beside busy loops on half the cores.
`summarize.py` prints the verdict.

## Raspberry Pi 3B

```bash
ARM=my-arm BUILD=1 HELIX_PERF_BUILD_TREE=$PWD \
  setsid nohup scripts/screensaver-perf/arm_measure.sh > "$HELIX_PERF_SCRATCH/my-arm.log" 2>&1 < /dev/null &
```

The arm claims `device:pi3b` with `scripts/helix-claim`, deploys, smoke-tests every saver for
crashes, runs three measurement passes and three load-gate passes, and writes
`$HELIX_PERF_SCRATCH/my-arm_summary.txt`. `ENV_EXTRA="HELIX_SCREENSAVER_LEVEL=1"` runs the arm
with those variables in a systemd drop-in, which the arm removes (restarting the app) when it
finishes. An arm has two halves and they take DIFFERENT variables: `WORKLOADS` selects the
measurement passes (default `idle toasters starfield pipes`) and `SAVERS` selects the load gates
(default `off toasters starfield pipes`). Setting only one leaves the other at its default, so
`SAVERS="off fireworks"` gates fireworks while still measuring the default four. Workload names
come from `perf_saver_type` in `perf_env.sh`: `idle`, `off` and `ui` run no saver, and `toasters`,
`starfield`, `pipes`, `bounce` and `fireworks` each run that saver.

Before touching the Pi the arm asks the Moonraker in the app's settings for the print state and
stops unless the printer is idle (`PRINT_STATE` in the log). Every arm runs with
`HELIX_EGL_VSYNC=1 HELIX_LOOP_MIN_SLEEP_MS=1`, which keep 16 ms frames even. They are set rather
than detected, because the app logs neither when it equals the default; the `APP_ENV ok` line
confirms the running app carries them and `ENV_EXTRA` (`env-<ARM>.txt` holds its environment).
A step that drives the Pi by hand runs its commands between `perf_pi3b_take "<reason>"` and
`perf_pi3b_release` from `perf_env.sh`, which claim `device:pi3b` and make the same check.

Every pass appends a `THERMAL` line per workload (`vcgencmd measure_temp`, `get_throttled` and
the ARM clock) and every gate run one sampled mid-run. The Pi 3B throttles under the load gate
(80 to 84 C, `throttled=0x20002`, the ARM clock down to 818 MHz), and a throttled run is not
comparable with an unthrottled one. `summarize.py` ignores these lines; report the hottest
reading and the count of CURRENTLY throttled readings beside an arm's numbers.

`get_throttled` splits into current state in bits 0-3 and "has occurred since boot" in bits
16-19, and the high bits never clear until a reboot. A board that throttled once therefore
reports a non-zero value for the rest of its uptime, so counting any value other than `0x0`
condemns every later run: a clean set of passes on a Pi 3B that had thermally capped earlier
in the day reads `throttled=0x20000` on all fifteen samples while its ARM clock never leaves
1200 MHz. Mask the low nibble instead:

```bash
grep -o "temp=[0-9.]*" "$HELIX_PERF_SCRATCH/results/my-arm.txt" | sort -t= -k2 -n | tail -n 1
n=0; for v in $(grep -o 'throttled=0x[0-9a-f]*' "$HELIX_PERF_SCRATCH/results/my-arm.txt" | cut -d= -f2); do
  (( (v & 0xF) != 0 )) && n=$((n + 1))
done; echo "$n currently-throttled sample(s)"
```

Frames come from `strace` of the presenting ioctls: `DRM_IOCTL_MODE_PAGE_FLIP` on the dumb-DRM
binary and `DRM_IOCTL_MODE_ATOMIC` on the EGL binary.

## BusyBox boards (CC1, AD5M)

These boards have no cyclictest, stress-ng or python. Build the probe as a static armv7
binary in the CC1 toolchain image:

```bash
mkdir -p build/screensaver-perf
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/src -w /src helixscreen/toolchain-cc1 \
  arm-none-linux-gnueabihf-gcc -std=c99 -O2 -static \
  -o build/screensaver-perf/wakeup_probe-armv7 scripts/screensaver-perf/wakeup_probe.c -lrt
```

Then, with the app already running the workload to measure:

```bash
DUR=60 HELIX_PERF_PROBE=build/screensaver-perf/wakeup_probe-armv7 \
  scripts/screensaver-perf/embedded_gate.sh cc1-fireworks 1 level0
python3 scripts/screensaver-perf/summarize.py "$HELIX_PERF_SCRATCH/results/cc1-fireworks.txt"
```

Never measure while a print runs, and get the board owner's approval before stressing a printer.
