#!/bin/bash
# mtv3-bsp — overlay и patches платы MTV3 поверх Rockchip/Firefly SDK.
#
#   mtv3-bsp apply [--force]   наложить mtv3-bsp на SDK (ветка mtv3 в каждом затронутом репозитории)
#   mtv3-bsp export            выгрузить изменения из SDK обратно в mtv3-bsp (patches + overlay + pinned manifest)
#   mtv3-bsp status            что изменено относительно базы в отслеживаемых репозиториях
#   mtv3-bsp track <path>      начать отслеживать ещё один репозиторий SDK (например external/rknpu)
#   mtv3-bsp init-layout       создать пустую структуру mtv3-bsp
#
# Пути: SDK — $RV1126_SDK (или -s DIR), mtv3-bsp — $MTV3_BSP (или -b DIR).
#
# Контракт mtv3-bsp:
#   overlay/<путь в SDK>        НОВЫЕ файлы (dts, defconfig, BoardConfig, rootfs-overlay...); копируются 1:1
#                               и коммитятся в репозиторий SDK коммитом "mtv3-bsp: overlay"
#   patches/<repo>/NNNN-*.patch изменения СУЩЕСТВУЮЩИХ файлов репозитория <repo> (kernel, u-boot,
#                               buildroot, device/rockchip, external/rknpu ...), формат git format-patch
#   patches/<repo>/base.txt     база (коммит манифеста), на которую ложатся патчи — информационно
#   manifest/pinned.xml         `repo manifest -r`: точные ревизии всех проектов SDK на момент export
#
# В каждом затронутом репозитории SDK: тег mtv3-base = ревизия из манифеста, ветка mtv3 = база + патчи
# + overlay. Разработка ведётся коммитами в ветке mtv3, затем `mtv3-bsp export` и коммит в mtv3-bsp.
set -euo pipefail

SDK=${RV1126_SDK:-/proj/rv1126_sdk_ai}
BSP=${MTV3_BSP:-/work/mtv3-bsp}
OVERLAY_MSG="mtv3-bsp: overlay"
BASE_TAG=mtv3-base
BRANCH=mtv3

die() { echo "mtv3-bsp: $*" >&2; exit 1; }
log() { echo "[mtv3-bsp] $*"; }

while getopts "s:b:h" opt; do
    case $opt in
        s) SDK=$OPTARG ;;
        b) BSP=$OPTARG ;;
        h) sed -n '2,24p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
CMD=${1:-status}; shift || true

# ---------------------------------------------------------------- helpers
# repo path (относительно SDK) для файла/каталога внутри SDK; поднимаемся до существующего каталога
repo_of_path() {
    local p=$SDK/$1 d
    d=$p; while [ ! -d "$d" ]; do d=$(dirname "$d"); done
    local top
    top=$(git -C "$d" rev-parse --show-toplevel 2>/dev/null) || return 1
    case "$top" in
        "$SDK") echo "" ;;
        "$SDK"/*) echo "${top#"$SDK"/}" ;;
        *) return 1 ;;
    esac
}

overlay_list() {
    [ -d "$BSP/overlay" ] || return 0
    (cd "$BSP/overlay" && find . \( -type f -o -type l \) -not -name .gitkeep | sed 's|^\./||')
}

# репозитории, у которых есть overlay-файлы
overlay_repos() {
    overlay_list | while read -r f; do
        repo_of_path "$f" || die "overlay/$f: no git repository in SDK for this path"
    done | sort -u
}

# репозитории, у которых есть патчи
patch_repos() {
    [ -d "$BSP/patches" ] || return 0
    (cd "$BSP/patches" && find . -name '*.patch' -type f | xargs -r -n1 dirname | sed 's|^\./||') | sort -u
}

# репозитории, уже отслеживаемые (тег mtv3-base)
tracked_repos() {
    local top
    find "$SDK" -maxdepth 4 -name .git \( -type d -o -type f \) \
        -not -path "$SDK/.repo/*" -not -path "*/buildroot/output/*" -not -path "*/buildroot/dl/*" \
        2>/dev/null | while read -r g; do
        top=$(dirname "$g")
        if git -C "$top" rev-parse -q --verify "refs/tags/$BASE_TAG" >/dev/null 2>&1; then
            case "$top" in "$SDK") echo "" ;; *) echo "${top#"$SDK"/}" ;; esac
        fi
    done | sort -u
}

# overlay-файлы, принадлежащие репозиторию $1 (пути относительно репозитория)
overlay_files_of() {
    local repo=$1
    overlay_list | while read -r f; do
        if [ "$(repo_of_path "$f")" = "$repo" ]; then
            if [ -n "$repo" ]; then echo "${f#"$repo"/}"; else echo "$f"; fi
        fi
    done
}

# ревизия манифеста для репозитория (через repo, если есть), иначе HEAD
manifest_rev() {
    local repo=$1 rev=
    if [ -d "$SDK/.repo" ] && command -v repo >/dev/null 2>&1; then
        rev=$(cd "$SDK" && repo forall -r "^${repo}$" -c 'git rev-parse -q --verify "$REPO_LREV^{commit}" 2>/dev/null || git rev-parse -q --verify "$REPO_RREV^{commit}" 2>/dev/null' 2>/dev/null | tail -n1 || true)
    fi
    [ -n "$rev" ] || rev=$(git -C "$SDK/$repo" rev-parse HEAD)
    echo "$rev"
}

ensure_tracked() {
    local repo=$1 dir=$SDK/$1
    [ -d "$dir" ] || die "$repo: not found in SDK ($dir)"
    git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "$repo: not a git repository"
    if ! git -C "$dir" rev-parse -q --verify "refs/tags/$BASE_TAG" >/dev/null; then
        local rev; rev=$(manifest_rev "$repo")
        git -C "$dir" tag "$BASE_TAG" "$rev"
        log "${repo:-.}: base = $(git -C "$dir" describe --tags --always "$rev") ($rev)"
    fi
}

require_clean() {
    local repo=$1 dir=$SDK/$1
    if [ -n "$(git -C "$dir" status --porcelain --untracked-files=no)" ]; then
        die "${repo:-.}: uncommitted changes; commit, stash or run 'mtv3-bsp export' first"
    fi
    if [ -d "$(git -C "$dir" rev-parse --git-dir)/rebase-apply" ]; then
        die "${repo:-.}: git am in progress; finish it (git am --continue / --abort)"
    fi
}

# ---------------------------------------------------------------- commands
cmd_init_layout() {
    mkdir -p "$BSP/overlay" "$BSP/patches" "$BSP/manifest"
    [ -f "$BSP/README.md" ] || sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//' > "$BSP/README.md"
    touch "$BSP/overlay/.gitkeep" "$BSP/patches/.gitkeep"
    log "layout created in $BSP"
}

cmd_track() {
    local repo=${1:-}; [ -n "$repo" ] || die "usage: mtv3-bsp track <repo path>"
    repo=${repo%/}
    ensure_tracked "$repo"
    local dir=$SDK/$repo
    if ! git -C "$dir" rev-parse -q --verify "refs/heads/$BRANCH" >/dev/null; then
        git -C "$dir" checkout -q -B "$BRANCH" "$BASE_TAG"
        log "$repo: branch $BRANCH created at $BASE_TAG"
    else
        git -C "$dir" checkout -q "$BRANCH"
    fi
    mkdir -p "$BSP/patches/$repo"
}

cmd_apply() {
    local force=0
    [ "${1:-}" = "--force" ] && force=1
    [ -d "$BSP" ] || die "mtv3-bsp not found: $BSP (mount it: MTV3_BSP=/path sh run-sdk.sh)"
    [ -d "$SDK" ] || die "SDK not found: $SDK"

    local repos repo dir
    repos=$( { overlay_repos; patch_repos; } | sort -u)
    [ -n "$repos" ] || { log "nothing to apply: no overlay/ files and no patches/ in $BSP"; return 0; }

    while IFS= read -r repo; do
        dir=$SDK/$repo
        ensure_tracked "$repo"
        require_clean "$repo"
        if git -C "$dir" rev-parse -q --verify "refs/heads/$BRANCH" >/dev/null && \
           [ -n "$(git -C "$dir" rev-list "$BASE_TAG..$BRANCH" -- 2>/dev/null)" ] && [ $force -eq 0 ]; then
            die "${repo:-.}: branch $BRANCH already has commits on top of $BASE_TAG. Run 'mtv3-bsp export' to save them, or 'mtv3-bsp apply --force' to rebuild the branch from mtv3-bsp"
        fi
        log "${repo:-.}: checkout -B $BRANCH $BASE_TAG"
        git -C "$dir" checkout -q -B "$BRANCH" "$BASE_TAG"

        # 1) patches
        if [ -d "$BSP/patches/$repo" ] && ls "$BSP/patches/$repo"/*.patch >/dev/null 2>&1; then
            local n; n=$(ls "$BSP/patches/$repo"/*.patch | wc -l)
            log "${repo:-.}: git am ($n patches)"
            if ! git -C "$dir" am -3 --whitespace=nowarn "$BSP/patches/$repo"/*.patch; then
                die "${repo:-.}: patch failed. Fix conflicts, then 'git am --continue' (or --abort) in $dir and re-run"
            fi
        fi

        # 2) overlay
        local files f
        files=$(overlay_files_of "$repo")
        if [ -n "$files" ]; then
            while IFS= read -r f; do
                mkdir -p "$dir/$(dirname "$f")"
                if [ -n "$repo" ]; then cp -a "$BSP/overlay/$repo/$f" "$dir/$f"; else cp -a "$BSP/overlay/$f" "$dir/$f"; fi
                git -C "$dir" add -f -- "$f"
            done <<< "$files"
            if [ -n "$(git -C "$dir" status --porcelain --untracked-files=no)" ]; then
                git -C "$dir" commit -q -m "$OVERLAY_MSG"
                log "${repo:-.}: overlay committed ($(printf '%s\n' "$files" | wc -l) files)"
            else
                log "${repo:-.}: overlay already identical"
            fi
        fi
    done <<< "$repos"
    log "apply done"
}

cmd_export() {
    [ -d "$BSP" ] || die "mtv3-bsp not found: $BSP"
    mkdir -p "$BSP/overlay" "$BSP/patches" "$BSP/manifest"
    local repos repo dir
    repos=$( { tracked_repos; overlay_repos; patch_repos; } | sort -u)
    [ -n "$repos" ] || { log "no tracked repositories (tag $BASE_TAG); use 'mtv3-bsp track <path>' or 'apply'"; return 0; }

    while IFS= read -r repo; do
        dir=$SDK/$repo
        [ -d "$dir" ] || { log "${repo:-.}: missing in SDK, skipped"; continue; }
        ensure_tracked "$repo"
        if [ -n "$(git -C "$dir" status --porcelain --untracked-files=no)" ]; then
            log "WARNING ${repo:-.}: uncommitted changes are NOT exported (commit them on branch $BRANCH)"
        fi

        # overlay: обновить файлы из HEAD репозитория
        local files f excl=()
        files=$(overlay_files_of "$repo")
        if [ -n "$files" ]; then
            while IFS= read -r f; do
                excl+=(":(exclude)$f")
                if git -C "$dir" cat-file -e "HEAD:$f" 2>/dev/null; then
                    local dst; if [ -n "$repo" ]; then dst=$BSP/overlay/$repo/$f; else dst=$BSP/overlay/$f; fi
                    mkdir -p "$(dirname "$dst")"
                    git -C "$dir" show "HEAD:$f" > "$dst"
                    chmod --reference="$dir/$f" "$dst" 2>/dev/null || true
                else
                    log "WARNING ${repo:-.}: overlay file $f is absent in HEAD (deleted?) — kept in mtv3-bsp"
                fi
            done <<< "$files"
        fi

        # patches: всё между базой и HEAD, кроме overlay-файлов
        local pdir=${BSP:?}/patches/$repo
        mkdir -p "$pdir"
        find "${pdir:?}" -maxdepth 1 -name '*.patch' -type f -delete
        local n
        n=$(git -C "$dir" rev-list --count "$BASE_TAG..HEAD")
        if [ "$n" -gt 0 ]; then
            git -C "$dir" format-patch -q --no-signature --no-stat -o "$pdir" "$BASE_TAG..HEAD" -- . "${excl[@]}" >/dev/null
        fi
        {
            echo "repo: ${repo:-.}"
            echo "base: $(git -C "$dir" rev-parse "$BASE_TAG")  $(git -C "$dir" describe --tags --always "$BASE_TAG" 2>/dev/null)"
            echo "head: $(git -C "$dir" rev-parse HEAD) ($n commits since base)"
            echo "exported: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        } > "$pdir/base.txt"
        local np; np=$(find "$pdir" -maxdepth 1 -name '*.patch' | wc -l)
        local nf; nf=$(printf '%s' "$files" | grep -c . || true)
        log "${repo:-.}: $np patches, $nf overlay files"
    done <<< "$repos"

    if [ -d "$SDK/.repo" ] && command -v repo >/dev/null 2>&1; then
        if (cd "$SDK" && repo manifest -r -o "$BSP/manifest/pinned.xml" >/dev/null 2>&1); then
            log "manifest/pinned.xml updated"
        else
            log "WARNING: repo manifest -r failed"
        fi
    fi
    log "export done -> $BSP (review with git diff, then commit)"
}

cmd_status() {
    local repos repo dir
    repos=$(tracked_repos)
    [ -n "$repos" ] || { log "no tracked repositories in $SDK (tag $BASE_TAG)"; return 0; }
    while IFS= read -r repo; do
        dir=$SDK/$repo
        local br n dirty
        br=$(git -C "$dir" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')
        n=$(git -C "$dir" rev-list --count "$BASE_TAG..HEAD" 2>/dev/null || echo '?')
        dirty=$(git -C "$dir" status --porcelain --untracked-files=no | wc -l)
        printf '%-28s branch=%-8s commits=%-3s dirty=%s\n' "${repo:-.}" "$br" "$n" "$dirty"
        git -C "$dir" log --oneline "$BASE_TAG..HEAD" 2>/dev/null | sed 's/^/    /'
    done <<< "$repos"
}

case "$CMD" in
    apply)       cmd_apply "$@" ;;
    export)      cmd_export "$@" ;;
    status)      cmd_status "$@" ;;
    track)       cmd_track "$@" ;;
    init-layout) cmd_init_layout "$@" ;;
    -h|--help|help) sed -n '2,24p' "$0" ;;
    *) die "unknown command: $CMD (apply|export|status|track|init-layout)" ;;
esac
