#!/bin/sh
# Regenerate the app icon (src/app.ico) - a stylized isometric cube matching the
# app's colorful test cube, on a dark rounded square. Requires ImageMagick.
#   sh tools/gen_icon.sh
set -e
out=src/app.ico
tmp=$(mktemp --suffix=.png)
magick -size 512x512 xc:none \
  -fill '#14141b' -draw 'roundrectangle 20,20,492,492,72,72' \
  -stroke '#0a0a0d' -strokewidth 4 \
  -fill '#4d8cff' -draw 'polygon 256,116 388,192 256,268 124,192' \
  -fill '#e6444a' -draw 'polygon 124,192 256,268 256,436 124,360' \
  -fill '#36c45a' -draw 'polygon 256,268 388,192 388,360 256,436' \
  "$tmp"
magick "$tmp" -define icon:auto-resize=256,128,64,48,32,24,16 "$out"
rm -f "$tmp"
echo "wrote $out"
