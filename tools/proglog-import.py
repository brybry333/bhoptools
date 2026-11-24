#!/usr/bin/env python3
"""proglog-import.py: a player's Progress Log history from a server's progresslog plugin database into the book's
files on their own PC (the book keeps the log there since it stopped needing the plugin).

    proglog-import.py --db addons/sourcemod/data/sqlite/progresslog.sq3 --steamid STEAM_0:1:123
                      [--cookies addons/sourcemod/data/sqlite/clientprefs-sqlite.sq3] [--out DIR] [--dry-run]

--out: the book's folder (default: ~/.local/share/cstv/progresslog on Linux, %APPDATA%\\cstv\\progresslog on
Windows). Maps that already have a file there get the imported attempts and spots added (spots already there win),
nothing is removed. --cookies also takes the book's settings (autofinish, saveat, bin) when there's no settings.txt
yet. Both databases are only read.
"""
import argparse
import os
import sqlite3
import sys


def default_out():
    if os.name == 'nt':
        return os.path.join(os.environ['APPDATA'], 'cstv', 'progresslog')
    base = os.environ.get('XDG_DATA_HOME') or os.path.join(os.path.expanduser('~'), '.local', 'share')
    return os.path.join(base, 'cstv', 'progresslog')


def clean(s):   # what the book allows in a name (no tabs, quotes, ;, \, control characters), 31 characters
    return ''.join(c for c in (s or '') if ord(c) >= 0x20 and c not in '"\\;\t').strip()[:31]


def num(v):
    return -1.0 if v is None else float(v)


def read_file(path):
    """(entries by id, spots by lower name) of a map file the book wrote."""
    entries, spots = {}, {}
    if os.path.exists(path):
        for line in open(path, encoding='utf-8', errors='replace'):
            f = line.rstrip('\r\n').split('\t')
            if f[0] == 'e' and len(f) >= 10:
                entries[int(f[1])] = f
            elif f[0] == 's' and len(f) >= 4:
                spots[f[1].lower()] = f
    return entries, spots


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--db', required=True)
    ap.add_argument('--steamid', required=True, help='as the plugin stored it, e.g. STEAM_0:1:123')
    ap.add_argument('--cookies')
    ap.add_argument('--out', default=default_out())
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    db = sqlite3.connect('file:%s?mode=ro' % a.db, uri=True)
    maps = {}
    for row in db.execute('SELECT id, map, day, created, source, from_text, from_time, to_text, to_time, track FROM prog_entries '
                          'WHERE steamid = ? ORDER BY id', (a.steamid,)):
        maps.setdefault(row[1].lower(), [[], []])[0].append(row)
    for row in db.execute('SELECT map, name, time, time_hi FROM prog_spots WHERE steamid = ? AND time IS NOT NULL', (a.steamid,)):
        maps.setdefault(row[0].lower(), [[], []])[1].append(row)
    if not maps:
        sys.exit('nothing for %s in %s' % (a.steamid, a.db))
    if not a.dry_run:
        os.makedirs(a.out, exist_ok=True)
    for m, (rows, spots) in sorted(maps.items()):
        path = os.path.join(a.out, ''.join(c if c.isalnum() or c in '_-.' else '_' for c in m) + '.txt')
        have, have_spots = read_file(path)
        next_id = max(list(have) + [0]) + 1
        added = 0
        for (eid, _, day, created, src, ft_text, ft, tt_text, tt, track) in rows:
            # the server's ids are unique there; kept unless the file already uses one
            nid = eid if eid not in have else next_id
            next_id = max(next_id, nid + 1)
            have[nid] = ['e', str(nid), day, str(created), str(src), clean(ft_text), '%.3f' % num(ft), clean(tt_text), '%.3f' % num(tt),
                         str(track or 0)]
            added += 1
        for (_, name, lo, hi) in spots:
            name = clean(name)
            if name and name.lower() not in have_spots:
                have_spots[name.lower()] = ['s', name, '%.3f' % lo, '%.3f' % num(hi)]
        print('%s: %d attempts imported, %d spots -> %s' % (m, added, len(have_spots), path))
        if a.dry_run:
            continue
        with open(path + '.part', 'w', encoding='utf-8', newline='\n') as f:
            f.write('progresslog 1\n')
            for k in sorted(have):
                f.write('\t'.join(have[k]) + '\n')
            for s in have_spots.values():
                f.write('\t'.join(s) + '\n')
        os.replace(path + '.part', path)
    settings = os.path.join(a.out, 'settings.txt')
    if a.cookies and not os.path.exists(settings):
        c = sqlite3.connect('file:%s?mode=ro' % a.cookies, uri=True)
        vals = dict(c.execute("SELECT c.name, p.value FROM sm_cookie_cache p JOIN sm_cookies c ON c.id = p.cookie_id "
                              "WHERE p.player = ? AND c.name IN ('prog_autofinish', 'prog_saveat', 'prog_bin')", (a.steamid,)))
        auto, at = vals.get('prog_autofinish', '1') or '1', vals.get('prog_saveat', '0') or '0'
        b = int(vals.get('prog_bin') or 10)
        text = 'autofinish %d\nsaveat %d\nbin %d\ndaystart 5\n' % (auto != '0', at != '0', min(max(b, 5), 60))
        print('settings: ' + text.replace('\n', ', '))
        if not a.dry_run:
            open(settings, 'w').write(text)


if __name__ == '__main__':
    main()
