#!/bin/sh
# bhoptools for Linux CS:S. Run it from the unpacked bhoptools folder:  ./install.sh
# It copies bhoptools to ~/.local/share/bhoptools, puts CS:TV's screen files into CS:S, starts CS:TV's video helper as
# a user service, and prints the line for CS:S's launch options. Run it again after updating.
set -e
SRC=$(cd "$(dirname "$0")" && pwd)
DEST=${BHOPTOOLS_DIR:-$HOME/.local/share/bhoptools}
case $DEST in *" "*) echo "your folder has a space in it ($DEST): set BHOPTOOLS_DIR to one without"; exit 1;; esac

mkdir -p "$DEST"
# --remove-destination: a running game (and cstvd) keep the old files instead of having them rewritten under them
[ "$SRC" = "$DEST" ] || cp -r --remove-destination "$SRC"/. "$DEST"/
echo "installed to $DEST"

# CS:TV's screen materials and fonts: into every CS:S install found (custom folders are read at game start)
for steam in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
	vdf=$steam/steamapps/libraryfolders.vdf
	[ -f "$vdf" ] || continue
	{ echo "$steam"; sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"/\1/p' "$vdf"; } | while IFS= read -r lib; do
		game="$lib/steamapps/common/Counter-Strike Source/cstrike"
		[ -d "$game" ] || continue
		mkdir -p "$game/custom/cstv"
		cp -r "$DEST/cstv/assets/materials" "$DEST/cstv/assets/resource" "$game/custom/cstv/"
		echo "CS:TV files -> $game/custom/cstv"
		# the Progress Log's Minecraft book: from a Minecraft install here, else Mojang's download server (Mojang's
		# files never ship with bhoptools); nothing to do when they're there already
		rm -f "$game/custom/cstv/resource/cstv/.mcassets-tried"
		python3 -I "$DEST/cstv/mcassets.py" --auto --out "$game/custom/cstv" 2>&1 | sed 's/^/  Progress Log book: /' || true
	done
done

# CS:TV's video helper runs outside Steam's runtime: a user service that starts when the game connects
launch=%command%
if command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
	units=$HOME/.config/systemd/user
	mkdir -p "$units"
	cat > "$units/cstvd.socket" <<EOF
[Unit]
Description=CS:TV helper socket (starts cstvd when the game connects)

[Socket]
ListenStream=/tmp/cstv/ctl.sock
SocketMode=0600
DirectoryMode=0700

[Install]
WantedBy=sockets.target
EOF
	cat > "$units/cstvd.service" <<EOF
[Unit]
Description=CS:TV helper (streams videos for in-game CS:S screens)
Requires=cstvd.socket
After=cstvd.socket

[Service]
ExecStart=$DEST/cstv/helper/cstvd
Environment=CSTV_QUIT_WITHOUT_GAME=1
Restart=on-failure
RestartSec=2
EOF
	systemctl --user daemon-reload
	systemctl --user enable --now cstvd.socket >/dev/null 2>&1 && echo "CS:TV helper: ready"
else
	echo "no systemd user services here: the launch line below starts CS:TV's video helper through cstv-launch"
	launch="$DEST/cstv/cstv-launch %command%"
fi

missing=
for p in mpv yt-dlp streamlink python3; do command -v $p >/dev/null 2>&1 || missing="$missing $p"; done
[ -z "$missing" ] || echo "for CS:TV videos, install:$missing"
libs=$(ldd "$DEST/cstv/helper/cstvd" 2>/dev/null | sed -n 's/^[[:space:]]*\([^ ]*\) => not found.*/\1/p' | tr '\n' ' ')
[ -z "$libs" ] || echo "CS:TV's video helper can't start here, missing: $libs(a newer mpv / libmpv is needed)"

echo
echo "Steam > Counter-Strike: Source > Properties > Launch Options, paste:"
echo
echo "  LD_PRELOAD=\"$DEST/bhoptools.so:$DEST/cstv/client/libcstv.so:\$LD_PRELOAD\" $launch -insecure"
echo
