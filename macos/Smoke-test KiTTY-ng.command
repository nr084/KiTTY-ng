#!/bin/zsh
# Native checks with disposable profiles and synthetic keys; no server connection.
set -eu
umask 077
base=${0:A:h}
app="$base/KiTTY-ng.app"
helpers="$app/Contents/Helpers"
log="$base/kitty-ng-smoke-test.log"
work=$(mktemp -d /tmp/kitty-ng-smoke.XXXXXXXX)
trap 'result=$?; if (( result != 0 )); then cat "$log"; fi; rm -rf -- "$work"' EXIT
export KITTY_NG_PORTABLE_DIR="$work"
unset KITTY_NG_GUI_PROMPTS
runner=()
if [[ -n ${KITTY_NG_TEST_ARCH:-} ]]; then runner=(/usr/bin/arch "-$KITTY_NG_TEST_ARCH"); fi
{
  date
  sw_vers
  uname -m
  codesign --verify --deep --strict --verbose=2 "$app"
  for name in klink kscp ksftp kittygen kageant; do
    lipo -archs "$helpers/$name"
    "${runner[@]}" "$helpers/$name" -V
  done
  printf '\n' > "$work/empty-passphrase"
  "${runner[@]}" "$helpers/kittygen" -t ed25519 -C synthetic-smoke-key -o "$work/key.ppk" --new-passphrase "$work/empty-passphrase"
  test -s "$work/kitty-ng-macos.rnd"
  test ! -e "$work/putty.rnd"
  test ! -e "$work/kitty-ng-win.rnd"
  "${runner[@]}" "$helpers/kittygen" "$work/key.ppk" -L > "$work/public.key"
  grep '^ssh-ed25519 ' "$work/public.key"
  KITTY_NG_SELF_TEST=1 "${runner[@]}" "$app/Contents/MacOS/kitty-ng" --self-test-ui
  KITTY_NG_SELF_TEST=1 "${runner[@]}" "$app/Contents/MacOS/kitty-ng" --self-test-portable "$work/public.key"
  "${runner[@]}" "$helpers/kittygen" "$work/key.ppk" -O private-openssh-new -o "$work/key.openssh" --old-passphrase "$work/empty-passphrase" --new-passphrase "$work/empty-passphrase"
  test "$(stat -f %Lp "$work/key.openssh")" = 600
  "${runner[@]}" "$helpers/kittygen" --language ru -V
  "${runner[@]}" "$helpers/kittygen" --language en -V
  printf 'invalid snapshot' > "$work/invalid"
  if KITTY_NG_GUI_PROMPTS=1 "${runner[@]}" "$helpers/klink" --kitty-ng-conf stdin -batch -ssh test.invalid < "$work/invalid"; then
    echo 'FAIL: invalid snapshot accepted'; exit 1
  fi
  "${runner[@]}" "$helpers/kageant" --exec /usr/bin/true
  echo 'PASS: native helper smoke checks; GUI and real SSH/file transfers require manual testing'
} > "$log" 2>&1
cat "$log"
