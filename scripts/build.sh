#!/usr/bin/env bash
# Builds Complex Operations (complex_operations.dll, beside hellforge-mod.json) and, with --test, builds and runs every
# tests/*_test.c against tests/harness. The Linux equivalent of HellForge's own mod build: the same Zig target and warning
# flags, the SDK of a HellForge-Open checkout, and the tests as Windows executables under Wine.
# Usage: scripts/build.sh [--test] [--package] [--players 4|8] [--test-first-slot <1-7>] [--only <test id>]
#        [--hellforge <HellForge-Open checkout>] [--zig <zig>] [--no-dll] [--research]
# --research builds a DLL that reads the extra large world's HELLFORGE_* research switches. Peers must generate alike,
# so no other build reads them and a package is never built with it.
# --players sets how many players a session takes (default 4); the DLL and the tests are built for that count.
# --test-first-slot is a test aid for a PC with two game instances and never goes into a package: as host the mod
# gives joining peers slots and player indexes from that number on. It needs --players 8.
# Defaults: HELLFORGE_OPEN, else ~/HellForge; HELLFORGE_ZIG, else ~/.local/share/zig-native/zig.
set -euo pipefail
export LC_ALL=C

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
root=$(cd -- "$script_dir/.." && pwd -P)

test_mode=false
only=
hellforge=${HELLFORGE_OPEN:-}
zig=${HELLFORGE_ZIG:-}
no_dll=false
package=false
research=false
players=4
first_slot=0
while (($#)); do
    case $1 in
        --test)
            test_mode=true
            shift
            ;;
        --only)
            if (($# < 2)); then
                echo '--only needs a test id.' >&2
                exit 1
            fi
            only=$2
            shift 2
            ;;
        --hellforge)
            if (($# < 2)); then
                echo '--hellforge needs a HellForge-Open checkout.' >&2
                exit 1
            fi
            hellforge=$2
            shift 2
            ;;
        --zig)
            if (($# < 2)); then
                echo '--zig needs a path.' >&2
                exit 1
            fi
            zig=$2
            shift 2
            ;;
        --no-dll)
            no_dll=true
            shift
            ;;
        --players)
            if (($# < 2)) || [[ $2 != 4 && $2 != 8 ]]; then
                echo '--players needs 4 or 8.' >&2
                exit 1
            fi
            players=$2
            shift 2
            ;;
        --test-first-slot)
            if (($# < 2)) || [[ ! $2 =~ ^[1-7]$ ]]; then
                echo '--test-first-slot needs a slot from 1 to 7.' >&2
                exit 1
            fi
            first_slot=$2
            shift 2
            ;;
        --package)
            package=true
            shift
            ;;
        --research)
            research=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if ((first_slot != 0)) && $package; then
    echo '--test-first-slot is a test aid; a package is never built with it.' >&2
    exit 1
fi
if ((first_slot != 0 && players != 8)); then
    echo '--test-first-slot needs --players 8: a client with a player index above 3 never spawns in a build for four.' >&2
    exit 1
fi
if $research && $package; then
    echo '--research is for one PC; a package is never built with it.' >&2
    exit 1
fi
defines=(-DCO_PLAYERS="$players" -DCO_TEST_FIRST_SLOT="$first_slot")
if $research; then
    defines+=(-DCO_RESEARCH)
fi

if [[ -z $hellforge ]]; then
    hellforge=$HOME/HellForge
fi
sdk=$hellforge/native/mod-sdk
# After the SDK: its overlay header, for a checkout whose SDK has none yet.
includes=(-I "$sdk" -I "$root/src/xl/sdk-fallback" -I "$root/src")
if [[ ! -f $sdk/hellforge_mod.h ]]; then
    echo "No HellForge mod SDK at $sdk (pass --hellforge <HellForge-Open checkout>)." >&2
    exit 1
fi
if [[ -z $zig ]]; then
    zig=$HOME/.local/share/zig-native/zig
fi
if [[ ! -e $zig ]]; then
    echo 'No zig found (pass --zig, or set HELLFORGE_ZIG).' >&2
    exit 1
fi

build=$root/build
mkdir -p -- "$build"
export ZIG_GLOBAL_CACHE_DIR="$build/zig-cache"
export ZIG_LOCAL_CACHE_DIR="$build/zig-cache"
# The compiler's temporary files stay in build/ too.
tmp=$build/tmp
mkdir -p -- "$tmp"
export TMPDIR="$tmp" TMP="$tmp" TEMP="$tmp"

# src/xl/proto holds files that are parts of one unit (src/xl/co_xl.c includes them); none is compiled on its own.
mapfile -t sources < <(find "$root/src" -type f -name '*.c' -not -path "$root/src/xl/proto/*" -print | sort)
if ((${#sources[@]} == 0)); then
    echo "No C sources under $root/src." >&2
    exit 1
fi
# Generated C sources in data/, if any, compile into the DLL and the tests.
mapfile -t generated < <(find "$root/data" -maxdepth 1 -type f -name '*.c' -print | sort)
if ((${#generated[@]} != 0)); then
    sources+=("${generated[@]}")
fi

dll=$root/complex_operations.dll
if ! $no_dll; then
    if ! "$zig" cc -std=c11 -target x86_64-windows-gnu -shared -O2 -Wall -Werror -Wno-unused-parameter ${CO_CFLAGS:-} \
        "${defines[@]}" "${includes[@]}" "${sources[@]}" -o "$dll"; then
        echo 'The DLL did not compile.' >&2
        exit 1
    fi
    find "$root" -maxdepth 1 -type f \( -iname '*.lib' -o -iname '*.pdb' \) -delete
    printf 'Built %s (%s bytes) for %s players\n' "$dll" "$(stat -c '%s' "$dll")" "$players"
    if ((first_slot != 0)); then
        printf 'TEST BUILD: joining peers get slots and player indexes from %s on. Not for players.\n' "$first_slot"
    fi
fi

if $test_mode && [[ ! -d $root/tests ]]; then
    echo 'No tests yet (tests/ does not exist).'
elif $test_mode; then
    default_wine="$HOME/.local/share/Steam/steamapps/common/Proton - Experimental/files/bin/wine"
    wine=${WINE:-$default_wine}
    if [[ ! -e $wine ]]; then
        echo "No wine found at $wine (set WINE to Proton Experimental's files/bin/wine)." >&2
        exit 1
    fi
    export WINEPREFIX="$build/wineprefix"
    export WINEDEBUG=-all

    out=$build/tests
    mkdir -p -- "$out"
    mapfile -t harness < <(find "$root/tests/harness" -maxdepth 1 -type f -name '*.c' -print | sort)

    # One test case: its id, its test file, and every src and harness source. Built with -DCO_TEST, run in build/tests.
    failed=0
    run_case() {
        local id=$1
        local test_file=$2
        local exe=$out/${id}_test.exe
        if ! "$zig" cc -std=c11 -target x86_64-windows-gnu -O2 -g -Wall -Werror -Wno-unused-parameter -DCO_TEST \
            "${defines[@]}" "${includes[@]}" -I "$root/tests" \
            "$test_file" "${sources[@]}" "${harness[@]}" -o "$exe"; then
            printf '%s: did not compile\n' "$id"
            failed=$((failed + 1))
            return
        fi
        local code=0
        set +e
        (cd "$out" && "$wine" "$exe") | sed 's/^/  /'
        code=${PIPESTATUS[0]}
        set -e
        if ((code != 0)); then
            printf '%s: FAILED\n' "$id"
            failed=$((failed + 1))
        else
            printf '%s: passed\n' "$id"
        fi
    }

    while IFS= read -r file; do
        base=${file##*/}
        id=${base%_test.c}
        if [[ -n $only && ${only,,} != "${id,,}" ]]; then
            continue
        fi
        run_case "$id" "$file"
    done < <(find "$root/tests" -maxdepth 1 -type f -name '*_test.c' -print | sort)

    if ((failed != 0)); then
        printf '%s test file(s) failed.\n' "$failed" >&2
        exit 1
    fi
fi

# The files a player installs, as one zip. HellForge builds data/ from the recipe on the player's PC.
if $package; then
    version=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$root/hellforge-mod.json")
    files=(hellforge-mod.json complex_operations.dll hellforge-recipe.json hellforge-recipe.literals)
    for file in "${files[@]}"; do
        if [[ ! -f $root/$file ]]; then
            echo "$file is missing; nothing packaged." >&2
            exit 1
        fi
    done
    mkdir -p -- "$build/package"
    zip_file=$build/package/complex-operations-$version.zip
    rm -f -- "$zip_file"
    (cd "$root" && zip -q "$zip_file" "${files[@]}")
    printf 'Packaged %s (%s bytes)\n' "$zip_file" "$(stat -c '%s' "$zip_file")"
fi
