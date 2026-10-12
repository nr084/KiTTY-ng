#!/bin/zsh
# Diagnostic launch only; zombies deliberately retain deallocated objects.
set -euo pipefail
base="${0:A:h}"
app="$base/KiTTY-ng.app/Contents/MacOS/kitty-ng"
log="$base/kitty-ng-diagnostic.log"
if [[ ! -x "$app" ]]; then
    print -u2 "KiTTY-ng.app must be next to this command file."
    exit 1
fi
export NSZombieEnabled=YES
export NSAutoreleaseFreedObjectCheckEnabled=YES
export MallocScribble=1
print "Diagnostic launch. Log: $log"
"$app" 2>&1 | /usr/bin/tee "$log"
