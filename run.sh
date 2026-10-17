#!/bin/zsh
set -e

AC_APP=${AC_APP:-/Applications/AssaultCube.app}
DIR=${0:A:h}

if [[ ! -d "$AC_APP/Contents/gamedata" ]]; then
    echo "AssaultCube not found at $AC_APP (set AC_APP=/path/to/AssaultCube.app)"
    exit 1
fi

make -C "$DIR" -s

cd "$AC_APP/Contents/gamedata"
DYLD_INSERT_LIBRARIES="$DIR/accheat.dylib" exec ./assaultcube.app/Contents/MacOS/assaultcube \
    --home="$HOME/Library/Application Support/assaultcube/v1.3" --init "$@"
