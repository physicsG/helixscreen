# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared setup for the script tests."""
import os

# Git exports GIT_DIR, GIT_INDEX_FILE and friends to hooks, and the commit hook
# runs this suite. A test that builds a throwaway repo with them still set
# writes its config and index into the repository being committed.
for _var in [v for v in os.environ if v.startswith("GIT_")]:
    del os.environ[_var]
