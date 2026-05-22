#!/bin/sh

# Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
IMAGE_NAME=${IMAGE_NAME:-rttr-quantity-extractor:latest}
RTTR_REVISION=${RTTR_REVISION:-$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || printf '0000000000000000000000000000000000000000')}
CONTEXT_DIR=${TMPDIR:-/tmp}/rttr-quantity-extractor-context.$$

cleanup() {
    rm -rf "$CONTEXT_DIR"
}
trap cleanup EXIT INT TERM

mkdir -p "$CONTEXT_DIR"
tar -C "$REPO_ROOT" \
    --exclude='./.git' \
    --exclude='./.codex' \
    --exclude='./.claude' \
    --exclude='./.idea' \
    --exclude='./build' \
    --exclude='./build-*' \
    --exclude='./cmake-build-*' \
    --exclude='./tmp' \
    -cf - . | tar -C "$CONTEXT_DIR" -xf -

docker build \
    --build-arg "RTTR_REVISION=$RTTR_REVISION" \
    --file "$CONTEXT_DIR/extras/quantity-extractor/Dockerfile" \
    --tag "$IMAGE_NAME" \
    "$CONTEXT_DIR"

printf 'Built image %s\n' "$IMAGE_NAME"
