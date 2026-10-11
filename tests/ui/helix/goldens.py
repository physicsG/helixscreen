# SPDX-License-Identifier: GPL-3.0-or-later

"""Golden-image comparison. Knows nothing about HelixScreen — just images."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image


class GoldenMismatch(AssertionError):
    """An image did not match its golden, or no golden exists yet."""


@dataclass
class ComparisonResult:
    matches: bool
    reason: str
    diff: Image.Image | None = None


def compare_images(actual: Image.Image, expected: Image.Image) -> ComparisonResult:
    """Exact pixel comparison between two already-loaded images.

    The reusable core: compare() below is this plus loading `expected` from a
    golden file, and a caller checking two live captures against each other
    (no golden file involved) uses this directly.
    """
    actual = actual.convert("RGBA")
    expected = expected.convert("RGBA")

    if actual.size != expected.size:
        return ComparisonResult(
            False,
            f"size differs: actual {actual.width}x{actual.height}, "
            f"expected {expected.width}x{expected.height}",
        )

    a = np.asarray(actual, dtype=np.int16)
    b = np.asarray(expected, dtype=np.int16)
    delta = np.abs(a - b)
    differing = int(np.count_nonzero(delta.any(axis=2)))
    if differing == 0:
        return ComparisonResult(True, "identical")

    total = actual.width * actual.height
    # Amplify so a 1-value channel drift is visible to a human opening the file.
    amplified = np.clip(delta.sum(axis=2) * 8, 0, 255).astype(np.uint8)
    diff_img = Image.fromarray(amplified, mode="L").convert("RGBA")
    return ComparisonResult(
        False,
        f"{differing} pixel{'s' if differing != 1 else ''} "
        f"{'differ' if differing != 1 else 'differs'} "
        f"({differing / total:.4%} of {total})",
        diff_img,
    )


def compare(actual: Image.Image, golden_path: Path) -> ComparisonResult:
    """Exact pixel comparison. Freeze + stable capture is what makes this viable."""
    with Image.open(golden_path) as g:
        golden = g.convert("RGBA").copy()
    return compare_images(actual, golden)


def assert_golden(actual: Image.Image, name: str, *, goldens_dir: Path,
                  artifacts_dir: Path, accept: bool) -> None:
    """Compare against `<goldens_dir>/<name>.png`, or write it when accepting."""
    golden_path = Path(goldens_dir) / f"{name}.png"

    if accept:
        golden_path.parent.mkdir(parents=True, exist_ok=True)
        actual.save(golden_path)
        return

    if not golden_path.exists():
        # Deliberately not auto-created: a golden written on its first run
        # asserts nothing, and nobody ever goes back to review it.
        raise GoldenMismatch(
            f"No golden at {golden_path}. Review the screen, then create it with "
            f"`pytest --accept-goldens`."
        )

    result = compare(actual, golden_path)
    if result.matches:
        return

    artifacts_dir = Path(artifacts_dir)
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    actual_path = artifacts_dir / f"{name}.actual.png"
    actual.save(actual_path)
    detail = f"  actual: {actual_path}"
    if result.diff is not None:
        diff_path = artifacts_dir / f"{name}.diff.png"
        result.diff.save(diff_path)
        detail += f"\n  diff:   {diff_path}"

    raise GoldenMismatch(
        f"{name} does not match its golden: {result.reason}\n"
        f"  golden: {golden_path}\n{detail}\n"
        f"If the change is intended, re-run with --accept-goldens."
    )
