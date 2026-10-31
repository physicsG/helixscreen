# SPDX-License-Identifier: GPL-3.0-or-later
#
# One Moonraker vintage for the HelixPrint plugin host harness
# (scripts/plugin-host-matrix.sh). Moonraker's source and its Python
# dependencies only: no Klipper, so Moonraker runs with klippy disconnected.
# The layout a cell uses (where the checkout sits, who runs it, a chroot) is
# built at container start by cell.sh, which is mounted rather than baked in,
# so editing the harness needs no image rebuild.
ARG PYTHON_IMAGE=python:3.11-slim-bookworm
FROM ${PYTHON_IMAGE}

ARG MOONRAKER_REF=master
ARG MOONRAKER_REPO=https://github.com/Arksine/moonraker.git

# gcc and the -dev packages cover dependencies with no wheel for this Python;
# libsodium is libnacl's runtime; bubblewrap reproduces ProtectSystem=strict.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git gcc libc6-dev libffi-dev libjpeg-dev zlib1g-dev \
        libsodium23 bubblewrap procps \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --quiet "$MOONRAKER_REPO" /opt/moonraker-src \
    && git -C /opt/moonraker-src checkout --quiet "$MOONRAKER_REF" \
    && git -C /opt/moonraker-src log --oneline -1

RUN cd /opt/moonraker-src \
    && pip install --no-cache-dir --disable-pip-version-check -r scripts/moonraker-requirements.txt

RUN useradd -m -u 1000 -s /bin/bash pi

# The Forge-X layout runs Moonraker chrooted into a copy of this filesystem,
# Python and Moonraker's dependencies included.
RUN mkdir -p /opt/fx-root \
    && for d in bin sbin lib lib32 lib64 libx32 usr etc; do \
           if [ -e "/$d" ] || [ -L "/$d" ]; then cp -a "/$d" /opt/fx-root/; fi; \
       done
