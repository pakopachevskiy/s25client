#!/bin/sh

# Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

usage() {
    cat >&2 <<EOF
Usage: $0 <save-file.sav> [output-dir]

Environment:
  IMAGE_NAME  Docker image to run. Defaults to rttr-quantity-extractor:latest.
EOF
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage
    exit 1
fi

SAVE_PATH=$1
if [ ! -f "$SAVE_PATH" ]; then
    printf 'Savegame file not found: %s\n' "$SAVE_PATH" >&2
    exit 1
fi

SAVE_DIR=$(CDPATH= cd -- "$(dirname -- "$SAVE_PATH")" && pwd)
SAVE_FILE=$(basename -- "$SAVE_PATH")

if [ "$#" -eq 2 ]; then
    OUTPUT_DIR=$2
else
    OUTPUT_DIR=$SAVE_DIR/quantity-extractor-output
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR=$(CDPATH= cd -- "$OUTPUT_DIR" && pwd)

IMAGE_NAME=${IMAGE_NAME:-rttr-quantity-extractor:latest}

docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "$SAVE_DIR:/input:ro" \
    --volume "$OUTPUT_DIR:/output" \
    "$IMAGE_NAME" \
    "/input/$SAVE_FILE" \
    /output

printf 'Wrote quantity snapshot files to %s\n' "$OUTPUT_DIR"
