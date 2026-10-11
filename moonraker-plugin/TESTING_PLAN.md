# helix_print Testing and Integration Plan

## Overview

This document outlines the testing strategy for the helix_print Moonraker plugin and its integration with HelixScreen.

---

## 1. Unit Tests

### 1.1 C++ Unit Tests (`tests/unit/test_helix_print_api.cpp`)

| Test Category | Description | Status |
|---------------|-------------|--------|
| Plugin Detection | `has_helix_plugin()` initial state | ✅ Written |
| Plugin Detection | `check_helix_plugin()` with disconnected client | ✅ Written |
| Input Validation | Path traversal rejection | ✅ Written |
| Input Validation | Newline injection rejection | ✅ Written |
| Input Validation | Valid filename acceptance | ✅ Written |
| Input Validation | Subdirectory path handling | ✅ Written |
| Data Structures | `ModifiedPrintResult` structure | ✅ Written |
| Edge Cases | Empty modifications list | ✅ Written |
| Edge Cases | Multiple modifications | ✅ Written |
| Edge Cases | Large G-code content | ✅ Written |

**Run with:**
```bash
cd helixscreen-helix-print
make test
# Or run specific test:
./build/bin/tests --test-case="HelixPrint*"
```

### 1.2 Python Unit Tests (`moonraker-plugin/tests/test_helix_print.py`)

| Test Category | Description | Status |
|---------------|-------------|--------|
| Initialization | Component loads successfully | ✅ Written |
| Initialization | Default config values | ✅ Written |
| Initialization | Custom config values | ✅ Written |
| Initialization | Endpoints registered | ✅ Written |
| Initialization | Event handlers registered | ✅ Written |
| Status API | Returns configuration | ✅ Written |
| Print API | Rejects missing original file | ✅ Written |
| Print API | Creates temp file | ✅ Written |
| Print API | Creates symlink | ✅ Written |
| Print API | Starts print with symlink | ✅ Written |
| Print API | Disabled returns error | ✅ Written |
| Symlinks | Replaces existing symlink | ✅ Written |
| Tracking | Tracks active print | ✅ Written |
| Paths | Handles subdirectory paths | ✅ Written |
| History ID | `history:history_changed` "added" records the id | ✅ Written |
| History ID | Klipper print_stats never supplies one | ✅ Written |
| History ID | Another print's filename is ignored | ✅ Written |
| History ID | "finished" action and an id-less payload are ignored | ✅ Written |
| History ID | The handler is registered | ✅ Written |
| History Patch | Filename rewritten to the original | ✅ Written |
| History Patch | Symlink dir prefix stripped | ✅ Written |
| History Patch | Other providers' auxiliary_data survives | ✅ Written |
| History Patch | Re-patching replaces our own entry | ✅ Written |
| History Patch | Hex-string and int job ids | ✅ Written |
| History Patch | Missing job, no job id, absent `save_job` | ✅ Written |
| History Patch | A throwing history component never raises | ✅ Written |
| History Patch | Event id reaches the patch end to end | ✅ Written |

**Run with:**
```bash
cd helixscreen-helix-print/moonraker-plugin
pip install pytest pytest-asyncio
pytest tests/test_helix_print.py -v
```

---

## 2. Integration Tests

### 2.1 Mock Integration (HelixScreen)

Test the full flow using HelixScreen's mock Moonraker client:

```bash
./build/bin/helix-screen --test -vv &
./build/bin/helix-screen ctl navigate print-select
```

**Manual Test Steps:**
1. Navigate to print-select panel
2. Select a G-code file with bed leveling
3. Uncheck "Bed Leveling" in pre-print options
4. Click "Print"
5. **Expected**: Log shows "helix_print plugin not available - using legacy flow"
6. **Verify**: Print starts, thumbnail shows correctly

### 2.2 Full Integration (Real Moonraker)

**Prerequisites:**
- Moonraker instance (can be on a Pi or local Docker)
- helix_print plugin installed

**Setup:**
```bash
# On Moonraker host:
cd ~/moonraker-helix-print
./install.sh

# Add to moonraker.conf:
[helix_print]
enabled: True

# Restart Moonraker:
sudo systemctl restart moonraker
```

**Test Steps:**

| # | Test | Command / Action | Expected Result |
|---|------|------------------|-----------------|
| 1 | Plugin Detection | `curl http://moonraker:7125/server/helix/status` | JSON with `enabled: true` |
| 2 | Plugin in HelixScreen | Start HelixScreen, check logs | "helix_print plugin available" |
| 3 | Modified Print | Select file, disable bed leveling, print | Print starts with symlink path |
| 4 | Klipper print_stats | Query `printer.print_stats` during print | filename = `.helix_print/original.gcode` |
| 5 | History Entry | After print, check history | filename = `original.gcode` (patched) |
| 6 | Symlink Cleanup | After print completes | Symlink deleted immediately |
| 7 | Temp Cleanup | Wait cleanup_delay | Temp file deleted |

---

## 3. Test Environments

### 3.1 Local Development (macOS)

```
HelixScreen (SDL) ──mock──> MoonrakerClientMock
```
- Tests UI flow and fallback path
- No real Moonraker needed
- Fast iteration

### 3.2 Local Docker Moonraker

```
HelixScreen (SDL) ──websocket──> Moonraker (Docker)
                                      │
                               helix_print plugin
```

**Docker setup:**
```bash
# Build Moonraker with plugin
docker build -t moonraker-helix -f docker/moonraker-test.Dockerfile .
docker run -p 7125:7125 moonraker-helix
```

### 3.3 Real Pi Environment

```
HelixScreen (Pi) ──websocket──> Moonraker (same Pi or remote)
                                      │
                               helix_print plugin
                                      │
                                    Klipper
```

**Deploy steps:**
```bash
# From dev machine:
make remote-pi           # Build on build server
make deploy-pi           # Deploy to Pi

# On Pi:
cd ~/moonraker-helix-print
./install.sh
sudo systemctl restart moonraker
```

---

## 4. Test Matrix

| Scenario | Mock | Docker | Real Pi |
|----------|------|--------|---------|
| Plugin not installed (fallback) | ✅ | ✅ | ✅ |
| Plugin installed, enabled | ❌ | ✅ | ✅ |
| Plugin installed, disabled | ❌ | ✅ | ✅ |
| Unmodified print (direct) | ✅ | ✅ | ✅ |
| Modified print (plugin) | ❌ | ✅ | ✅ |
| Modified print (fallback) | ✅ | ✅ | ✅ |
| Print cancellation | ❌ | ✅ | ✅ |
| History patching | ❌ | ✅ | ✅ |
| Symlink cleanup | ❌ | ✅ | ✅ |
| Temp file cleanup | ❌ | ✅ | ✅ |
| Concurrent prints | ❌ | ✅ | ✅ |
| Moonraker restart | ❌ | ✅ | ✅ |

---

## 5. Regression Tests

After each change, verify:

1. **Fallback still works**: Uninstall plugin, start modified print
2. **Direct print works**: Print unmodified file (no checkboxes unchecked)
3. **Plugin detection**: Check logs on startup
4. **No breaking changes**: Run full test suite

---

## 6. Manual QA Checklist

### Pre-Release Checklist

- [ ] Unit tests pass (C++ and Python)
- [ ] Fallback flow works without plugin
- [ ] Plugin installs correctly
- [ ] Plugin enables/disables correctly
- [ ] Modified print works via plugin
- [ ] `print_stats.filename` shows symlink path during print
- [ ] History shows original filename after print
- [ ] Thumbnails load correctly
- [ ] Symlink deleted after print
- [ ] Temp file deleted after delay
- [ ] Multiple consecutive prints work
- [ ] Moonraker restart doesn't break in-progress state
- [ ] Error handling shows user-friendly messages

---

## 7. Known Limitations

1. **Symlink filesystem**: Requires POSIX-compliant filesystem
2. **History patching**: Depends on Moonraker history API (may need adjustment)
3. **Concurrent same-file**: First print wins symlink, second needs unique name
4. **Cleanup on crash**: May leave orphaned temp files (cleaned on next startup)

---

## 8. Performance Benchmarks

| Operation | Target | Notes |
|-----------|--------|-------|
| Plugin detection | <100ms | Async, non-blocking |
| start_modified_print | <500ms | Includes file write + symlink |
| History patching | <100ms | On print completion |
| Symlink cleanup | <10ms | Immediate on completion |

---

## 9. Security Considerations

- [x] Path traversal validation in C++ client
- [x] Path validation in Python plugin (uses existing file)
- [ ] Consider rate limiting for print_modified endpoint
- [ ] Audit temp file permissions (should be readable by Klipper)

---

## 10. Next Steps

1. Run C++ unit tests: `make test`
2. Run Python unit tests: `pytest -v`
3. Test fallback flow in mock mode
4. Set up Docker Moonraker for full integration
5. Deploy to real Pi for final validation
6. Create PR with test results
