#!/usr/bin/env python3
"""The Progress Log's Minecraft book: its pictures, fonts and sounds, out of Minecraft itself.

    mcassets.py --auto --out <CS:S>/cstrike/custom/cstv     what the launchers run: does nothing when the files are
                                                             there, else takes them from a Minecraft install on this
                                                             PC, else from Mojang's own download servers
    mcassets.py [--jar client.jar] [--assets <launcher>/assets] [--out DIR] [--preview DIR]     by hand

Writes into --out:
  resource/cstv/progbook.bin            the sprites, raw RGBA at their own size (the module upscales them), the
                                        font's glyph sheets (font/ascii.png, font/nonlatin_european.png) among them
  resource/cstv/progfont.txt            where each character is on them: "<hex code point> <sheet> <x> <y> <advance>"
                                        (sheet 0 ascii, 1 nonlatin, -1 a space; glyphs 8 px high, ascent 7)
  sound/progbook/flip1-3.wav, click.wav the book's page turn and the button click

These are Mojang's files: they never go in a repo or a release. Each player's PC gets them from Minecraft (an install
there, or the same downloads the Minecraft launcher makes: only the ~30 files needed are read out of the game's jar,
about 1 MB, not the whole 39 MB). Without them the window draws a plain book in VGUI's font.

The sounds are .ogg in Minecraft and the game plays .wav: ffmpeg converts them, or libmpv (CS:TV's video player,
--mpv <its library>), else the book stays silent.

progbook.bin: "PBK1", u32 count, then count x (char name[32], u16 w, u16 h, u32 offset into the pixels),
then the pixels (RGBA, row by row). Little-endian.
"""
import argparse
import colorsys
import ctypes
import ctypes.util
import glob
import hashlib
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile
import zlib

try:
    from PIL import Image
except ImportError:
    Image = None

HOME = os.path.expanduser('~')
APPDATA = os.environ.get('APPDATA', '')
GAME = os.path.join(HOME, '.local/share/Steam/steamapps/common/Counter-Strike Source/cstrike/custom/cstv')

# Downloads: the version the book was drawn from (so everyone's looks the same), else the newest release.
PIN = '26.2'
MANIFEST = 'https://piston-meta.mojang.com/mc/game/version_manifest_v2.json'
RESOURCES = 'https://resources.download.minecraft.net/'
RETRY_AFTER = 6 * 3600   # --auto after a failed try: wait this long before trying again (it runs at every launch)

OUTPUTS = ['resource/cstv/progbook.bin', 'resource/cstv/progfont.txt'] + \
          ['sound/progbook/%s.wav' % n for n in ('flip1', 'flip2', 'flip3', 'click')]


def log(*a):
    print(*a, flush=True)


# ---------------------------------------------------------------- where the jar comes from
class LocalJar:
    def __init__(self, path):
        self.where = path
        self.z = zipfile.ZipFile(path)

    def read(self, name):
        return self.z.read(name)

    def has(self, name):
        try:
            self.z.getinfo(name)
            return True
        except KeyError:
            return False


def http_get(url, rng=None, timeout=30):
    req = urllib.request.Request(url)
    if rng:
        req.add_header('Range', 'bytes=%d-%d' % rng)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


class RemoteJar:
    """The jar on Mojang's server, read with range requests: the zip's directory, then only the entries asked for."""

    def __init__(self, url, size):
        self.where = url
        self.url, self.size = url, size
        self.whole = None
        tail = min(size, 65557)
        status, t = http_get(url, (size - tail, size - 1))
        if status != 206:   # no ranges: the whole jar, then
            self.whole = zipfile.ZipFile(io.BytesIO(t if len(t) == size else http_get(url, timeout=600)[1]))
            return
        e = t.rfind(b'PK\x05\x06')
        if e < 0:
            raise IOError('no zip directory at the end of ' + url)
        count, cd_size, cd_off = struct.unpack('<HII', t[e + 10:e + 20])
        status, cd = http_get(url, (cd_off, cd_off + cd_size - 1))
        self.entries = {}
        p = 0
        while p + 46 <= len(cd) and cd[p:p + 4] == b'PK\x01\x02':
            method, csize, usize, nl, xl, cl, local = struct.unpack('<10xH8xIIHHH8xI', cd[p:p + 46])
            name = cd[p + 46:p + 46 + nl].decode('utf-8', 'replace')
            self.entries[name] = (method, csize, usize, local)
            p += 46 + nl + xl + cl

    def has(self, name):
        return name in self.whole.namelist() if self.whole else name in self.entries

    def read(self, name):
        if self.whole:
            return self.whole.read(name)
        method, csize, usize, local = self.entries[name]
        status, d = http_get(self.url, (local, local + 30 + 1024 + csize - 1))   # header + name/extra (short) + data
        if d[:4] != b'PK\x03\x04':
            raise IOError('bad zip entry ' + name)
        nl, xl = struct.unpack('<HH', d[26:30])
        data = d[30 + nl + xl:30 + nl + xl + csize]
        if len(data) < csize:
            data += http_get(self.url, (local + 30 + nl + xl + len(data), local + 30 + nl + xl + csize - 1))[1]
        if method == 0:
            return data
        if method == 8:
            return zlib.decompress(data, -15)
        raise IOError('unknown compression in ' + name)


def launcher_dirs():
    """Minecraft launchers' data folders: (libraries/versions root, assets folder)."""
    roots = [os.path.join(HOME, '.local/share/PrismLauncher'), os.path.join(HOME, '.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher'),
             os.path.join(HOME, '.local/share/multimc'), os.path.join(HOME, '.minecraft'),
             os.path.join(HOME, '.var/app/com.mojang.Minecraft/.minecraft')]
    if APPDATA:
        roots += [os.path.join(APPDATA, 'PrismLauncher'), os.path.join(APPDATA, '.minecraft'), os.path.join(APPDATA, 'MultiMC')]
    return [(r, os.path.join(r, 'assets')) for r in roots if os.path.isdir(r)]


def version_key(v):   # 1.21.11, 26.2: compare as numbers
    return [int(x) if x.isdigit() else 0 for x in v.replace('-', '.').split('.')]


def local_jars():
    """Client jars of installed Minecraft versions, newest first: Prism / MultiMC keep them under libraries, the
    official launcher under versions/<v>/<v>.jar."""
    found = []
    for root, assets in launcher_dirs():
        for p in glob.glob(os.path.join(root, 'libraries/com/mojang/minecraft/*/minecraft-*-client.jar')):
            found.append((os.path.basename(os.path.dirname(p)), p, assets))
        for p in glob.glob(os.path.join(root, 'versions/*/*.jar')):
            v = os.path.basename(os.path.dirname(p))
            if os.path.basename(p) == v + '.jar':
                found.append((v, p, assets))
    found.sort(key=lambda f: version_key(f[0]), reverse=True)
    return found


def mojang_version():
    """(client jar url, size, sha1, asset index url) of PIN, else the newest release."""
    m = json.loads(http_get(MANIFEST)[1])
    vs = {v['id']: v for v in m['versions']}
    v = vs.get(PIN) or vs[m['latest']['release']]
    meta = json.loads(http_get(v['url'])[1])
    c = meta['downloads']['client']
    return v['id'], c['url'], c['size'], meta['assetIndex']['url']


# ---------------------------------------------------------------- sprites
SPRITES = [
    # name in progbook.bin      texture in the jar
    ('page_forward', 'gui/sprites/widget/page_forward'),
    ('page_forward_hl', 'gui/sprites/widget/page_forward_highlighted'),
    ('page_backward', 'gui/sprites/widget/page_backward'),
    ('page_backward_hl', 'gui/sprites/widget/page_backward_highlighted'),
    ('button', 'gui/sprites/widget/button'),
    ('button_hl', 'gui/sprites/widget/button_highlighted'),
    ('button_off', 'gui/sprites/widget/button_disabled'),
    ('scroller', 'gui/sprites/widget/scroller'),
    ('scroller_bg', 'gui/sprites/widget/scroller_background'),
    ('tooltip_bg', 'gui/sprites/tooltip/background'),
    ('tooltip_frame', 'gui/sprites/tooltip/frame'),
    ('popup', 'gui/sprites/popup/background'),
    ('xp_bg', 'gui/sprites/hud/experience_bar_background'),
    ('xp_green', 'gui/sprites/hud/experience_bar_progress'),
    ('tab', 'gui/sprites/recipe_book/tab'),
    ('tab_on', 'gui/sprites/recipe_book/tab_selected'),
    ('icon_book', 'item/writable_book'),
    ('icon_compass', 'item/compass_16'),
]
OTHER_TEXTURES = ['gui/book', 'gui/sprites/hud/experience_bar_progress', 'item/filled_map', 'item/filled_map_markings',
                  'font/ascii', 'font/nonlatin_european']
FONT_JSON = ['font/include/default.json', 'font/include/space.json']


def needed_entries():
    return ['assets/minecraft/textures/%s.png' % t for t in [p for _, p in SPRITES] + OTHER_TEXTURES] + \
           ['assets/minecraft/' + j for j in FONT_JSON]


class Jar:
    def __init__(self, src):
        self.src = src

    def png(self, name):
        return Image.open(io.BytesIO(self.src.read('assets/minecraft/textures/' + name + '.png'))).convert('RGBA')

    def json(self, name):
        return json.loads(self.src.read('assets/minecraft/' + name))


def hue_shift(im, hue_of_x):
    """The XP bar in another colour: each pixel's hue turned by hue_of_x(x) turns (its shading kept)."""
    out = im.copy()
    px = out.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            h, sat, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            r2, g2, b2 = colorsys.hsv_to_rgb((h + hue_of_x(x)) % 1, sat, v)
            px[x, y] = (round(r2 * 255), round(g2 * 255), round(b2 * 255), a)
    return out


XP_HUE = 95 / 360   # the XP bar's green
RAINBOW_FRAMES = 12


def xp_bars(jar):
    """The XP bar's progress picture green (as it is), blue, red, and a rainbow: RAINBOW_FRAMES of them one
    under the other, the colours moving along a step each frame."""
    xp = jar.png('gui/sprites/hud/experience_bar_progress')
    out = [('xp_blue', hue_shift(xp, lambda x: 215 / 360 - XP_HUE)), ('xp_red', hue_shift(xp, lambda x: 1 - XP_HUE))]
    rainbow = Image.new('RGBA', (xp.width, xp.height * RAINBOW_FRAMES))
    for f in range(RAINBOW_FRAMES):
        rainbow.paste(hue_shift(xp, lambda x: x / xp.width - f / RAINBOW_FRAMES - XP_HUE), (0, f * xp.height))
    out.append(('xp_rainbow', rainbow))
    return out


def filled_map(jar):
    """The filled map icon: the map, and its markings tinted the colour the game gives them."""
    base = jar.png('item/filled_map')
    marks = jar.png('item/filled_map_markings')
    tint = (0x46, 0x40, 0x2E)
    mp = marks.load()
    for y in range(marks.height):
        for x in range(marks.width):
            r, g, b, a = mp[x, y]
            mp[x, y] = (r * tint[0] // 255, g * tint[1] // 255, b * tint[2] // 255, a)
    base.alpha_composite(marks)
    return base


def build_sprites(jar):
    out = []
    book = jar.png('gui/book')
    bb = book.getbbox()
    out.append(('book', book.crop(bb)))
    for name, path in SPRITES:
        out.append((name, jar.png(path)))
    out += xp_bars(jar)
    out.append(('icon_map', filled_map(jar)))
    out.append(('font_ascii', jar.png('font/ascii')))
    out.append(('font_nonlatin', jar.png('font/nonlatin_european')))
    return out, bb


def bin_bytes(sprites):
    head = bytearray(b'PBK1' + struct.pack('<I', len(sprites)))
    blob = bytearray()
    for name, im in sprites:
        assert len(name) < 32
        head += name.encode().ljust(32, b'\0') + struct.pack('<HHI', im.width, im.height, len(blob))
        blob += im.tobytes()
    return bytes(head + blob)


# ---------------------------------------------------------------- the font
SHEETS = {'font/ascii.png': 0, 'font/nonlatin_european.png': 1}


def font_table(jar):
    """[(code point, sheet, x, y, advance)] from the font's bitmap providers (the first with a char wins, as in the
    game; advance = the glyph's width + 1) and its spaces (sheet -1)."""
    out = {}
    spaces = {}
    for p in jar.json('font/include/default.json')['providers'] + jar.json('font/include/space.json')['providers']:
        if p['type'] == 'space':
            spaces.update(p['advances'])
            continue
        f = p.get('file', '').split(':', 1)[-1]
        if p['type'] != 'bitmap' or f not in SHEETS:
            continue
        im = jar.png(f.rsplit('.png', 1)[0])
        rows = p['chars']
        cw, ch = im.width // len(rows[0]), im.height // len(rows)
        px = im.load()
        for r, row in enumerate(rows):
            for c, chr_ in enumerate(row):
                if chr_ in ('\0', ' ') or ord(chr_) in out:
                    continue
                cols = [x for x in range(cw) if any(px[c * cw + x, r * ch + y][3] for y in range(ch))]
                if cols:
                    out[ord(chr_)] = (SHEETS[f], c * cw, r * ch, max(cols) + 2)
    for chr_, adv in spaces.items():
        out.setdefault(ord(chr_), (-1, 0, 0, adv))
    return sorted((cp,) + v for cp, v in out.items())


# ---------------------------------------------------------------- sounds
SOUNDS = [('flip1', 'minecraft/sounds/item/book/open_flip1.ogg'), ('flip2', 'minecraft/sounds/item/book/open_flip2.ogg'),
          ('flip3', 'minecraft/sounds/item/book/open_flip3.ogg'), ('click', 'minecraft/sounds/random/click.ogg')]


def sound_objects(assets, index_url):
    """{sound key: hash}: from the launcher's asset indexes, else the downloaded one (index_url() gives its url)."""
    objects = {}
    if assets:
        for idx in sorted(glob.glob(os.path.join(assets, 'indexes/*.json')), key=os.path.getmtime):
            try:
                objects.update(json.load(open(idx)).get('objects', {}))
            except (OSError, ValueError):
                pass
    if not all(k in objects for _, k in SOUNDS):
        objects.update(json.loads(http_get(index_url())[1]).get('objects', {}))
    return {k: objects[k]['hash'] for _, k in SOUNDS if k in objects}


def sound_bytes(assets, h):
    """The .ogg: the launcher's copy, else Mojang's (checked against its hash)."""
    if assets:
        p = os.path.join(assets, 'objects', h[:2], h)
        if os.path.exists(p):
            return open(p, 'rb').read()
    d = http_get(RESOURCES + h[:2] + '/' + h)[1]
    if hashlib.sha1(d).hexdigest() != h:
        raise IOError('download of sound %s is damaged' % h)
    return d


class Mpv:
    """libmpv as a converter: its pcm audio output writes a .wav, as fast as it can."""

    def __init__(self, path):
        self.lib = ctypes.CDLL(path)
        self.lib.mpv_create.restype = ctypes.c_void_p
        self.lib.mpv_initialize.argtypes = [ctypes.c_void_p]
        self.lib.mpv_set_option_string.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        self.lib.mpv_command.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p)]
        self.lib.mpv_wait_event.argtypes = [ctypes.c_void_p, ctypes.c_double]
        self.lib.mpv_wait_event.restype = ctypes.POINTER(ctypes.c_int)   # mpv_event starts with its id
        self.lib.mpv_terminate_destroy.argtypes = [ctypes.c_void_p]

    def convert(self, src, dst):
        h = self.lib.mpv_create()
        if not h:
            raise IOError('libmpv did not start')
        try:
            for k, v in (('config', 'no'), ('terminal', 'no'), ('vid', 'no'), ('ao', 'pcm'), ('ao-pcm-file', dst),
                         ('ao-pcm-fast', 'yes'), ('audio-channels', 'mono'), ('audio-samplerate', '44100'),
                         ('audio-format', 's16'), ('idle', 'no')):
                self.lib.mpv_set_option_string(h, k.encode(), v.encode())
            if self.lib.mpv_initialize(h) < 0:
                raise IOError('libmpv did not start')
            args = (ctypes.c_char_p * 3)(b'loadfile', src.encode(), None)
            self.lib.mpv_command(h, args)
            end = time.time() + 30
            while time.time() < end:
                ev = self.lib.mpv_wait_event(h, 1.0)[0]
                if ev in (1, 7):   # MPV_EVENT_SHUTDOWN, MPV_EVENT_END_FILE
                    break
        finally:
            self.lib.mpv_terminate_destroy(h)


def find_mpv(given):
    for p in [given, ctypes.util.find_library('mpv'), 'libmpv.so.2', 'libmpv.so']:
        if not p:
            continue
        try:
            return Mpv(p)
        except OSError:
            continue
    return None


def ogg_to_wav(ogg, dst, mpv_path):
    """True when dst was written (mono 16-bit 44.1 kHz)."""
    tmpd = tempfile.mkdtemp(prefix='mcassets')
    try:
        src = os.path.join(tmpd, 'in.ogg')
        out = os.path.join(tmpd, 'out.wav')
        open(src, 'wb').write(ogg)
        ffmpeg = shutil.which('ffmpeg')
        if ffmpeg:
            subprocess.run([ffmpeg, '-loglevel', 'error', '-y', '-i', src, '-ac', '1', '-ar', '44100', '-sample_fmt', 's16', out],
                           check=True, stdin=subprocess.DEVNULL)
        else:
            mpv = find_mpv(mpv_path)
            if not mpv:
                return False
            mpv.convert(src, out)
        if not os.path.exists(out) or os.path.getsize(out) < 100 or open(out, 'rb').read(4) != b'RIFF':
            return False
        shutil.move(out, dst + '.part')
        os.replace(dst + '.part', dst)
        return True
    finally:
        shutil.rmtree(tmpd, ignore_errors=True)


# ---------------------------------------------------------------- checking
def preview(sprites, d):
    os.makedirs(d, exist_ok=True)
    s = dict(sprites)
    book = s['book']
    spread = Image.new('RGBA', (book.width * 2, book.height))
    spread.paste(book.transpose(Image.FLIP_LEFT_RIGHT), (0, 0))
    spread.paste(book, (book.width, 0))
    spread.resize((spread.width * 5, spread.height * 5), Image.NEAREST).save(os.path.join(d, 'spread.png'))
    w = sum(im.width for _, im in sprites[1:]) + 4 * len(sprites)
    sheet = Image.new('RGBA', (w, max(im.height for _, im in sprites[1:])), (60, 60, 60, 255))
    x = 0
    for _, im in sprites[1:]:
        sheet.alpha_composite(im, (x, 0))
        x += im.width + 4
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(os.path.join(d, 'sprites.png'))
    log('previews in', d)


def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + '.part', 'wb') as f:
        f.write(data)
    os.replace(path + '.part', path)


def pick_source(a):
    """(jar source, assets folder or None, asset index url or None)."""
    if a.jar:
        return LocalJar(a.jar), a.assets, None
    for v, path, assets in local_jars():
        try:
            j = LocalJar(path)
        except (OSError, zipfile.BadZipFile):
            continue
        if all(j.has(n) for n in needed_entries()):
            log('Minecraft %s on this PC: %s' % (v, path))
            return j, assets, None
    if not a.auto and not a.download:
        sys.exit('no Minecraft install found (pass --jar, or --download to get the files from Mojang)')
    v, url, size, index = mojang_version()
    log("Minecraft %s from Mojang's download server (only the book's files)" % v)
    j = RemoteJar(url, size)
    missing = [n for n in needed_entries() if not j.has(n)]
    if missing:
        raise IOError('Minecraft %s has no %s' % (v, missing[0]))
    return j, None, index


def make(a):
    src, assets, index = pick_source(a)
    jar = Jar(src)
    out = a.out
    sprites, _ = build_sprites(jar)
    table = font_table(jar)
    font = ''.join('%x %d %d %d %d\n' % row for row in table).encode()
    sounds = []
    try:
        objects = sound_objects(assets, lambda: index or mojang_version()[3])
        for name, key in SOUNDS:
            if key in objects:
                sounds.append((name, sound_bytes(assets, objects[key])))
    except (OSError, ValueError) as e:
        log('no sounds:', e)
    # the pictures last: the book checks for progbook.bin, so its sounds are there by the time it finds it
    wrote = 0
    os.makedirs(os.path.join(out, 'sound/progbook'), exist_ok=True)
    for name, ogg in sounds:
        try:
            wrote += ogg_to_wav(ogg, os.path.join(out, 'sound/progbook', name + '.wav'), a.mpv)
        except (OSError, subprocess.CalledProcessError) as e:
            log('sound %s: %s' % (name, e))
    if sounds and not wrote:
        log('sounds not converted: needs ffmpeg or libmpv (the book works, silent)')
    write_file(os.path.join(out, 'resource/cstv/progfont.txt'), font)
    write_file(os.path.join(out, 'resource/cstv/progbook.bin'), bin_bytes(sprites))
    for old in ('progmc.ttf', 'progmc-bold.ttf'):   # (the TrueType tried first: VGUI never used it)
        if os.path.exists(os.path.join(out, 'resource/cstv', old)):
            os.remove(os.path.join(out, 'resource/cstv', old))
    log('%s: %d sprites, %d characters, %d sounds' % (out, len(sprites), len(table), wrote))
    if a.preview:
        preview(sprites, a.preview)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jar', help='a Minecraft client jar')
    ap.add_argument('--assets', help="the launcher's assets folder (for the sounds)")
    ap.add_argument('--out', default=GAME, help='cstrike/custom/cstv')
    ap.add_argument('--auto', action='store_true', help='only when missing; a Minecraft install, else Mojang')
    ap.add_argument('--download', action='store_true', help="no install needed: Mojang's download server")
    ap.add_argument('--force', action='store_true', help='--auto even when the files are there or it failed lately')
    ap.add_argument('--mpv', help='libmpv to convert the sounds with when there is no ffmpeg')
    ap.add_argument('--log', help='also write what happens to this file')
    ap.add_argument('--preview')
    a = ap.parse_args()
    if a.log:
        os.makedirs(os.path.dirname(os.path.abspath(a.log)), exist_ok=True)
        sys.stdout = sys.stderr = open(a.log, 'a', buffering=1, encoding='utf-8', errors='replace')
        log(time.strftime('--- %Y-%m-%d %H:%M:%S'))
    if Image is None:
        sys.exit("mcassets.py needs Pillow (python-pillow / pip install pillow)")
    res = os.path.join(a.out, 'resource/cstv')
    tried = os.path.join(res, '.mcassets-tried')
    lock = os.path.join(res, '.mcassets-lock')
    if a.auto and not a.force:
        if all(os.path.exists(os.path.join(a.out, f)) for f in OUTPUTS):
            return
        if os.path.exists(tried) and time.time() - os.path.getmtime(tried) < RETRY_AFTER:
            log('tried less than %d h ago (see above); --force to try now' % (RETRY_AFTER // 3600))
            return
    os.makedirs(res, exist_ok=True)
    try:   # one at a time (two game launches); a lock left by a killed run goes stale after 10 minutes
        if os.path.exists(lock) and time.time() - os.path.getmtime(lock) > 600:
            os.remove(lock)
        os.close(os.open(lock, os.O_CREAT | os.O_EXCL))
    except FileExistsError:
        log('another mcassets.py is running')
        return
    try:
        make(a)
        if os.path.exists(tried):
            os.remove(tried)
    except Exception as e:   # (any: the launchers only care that it's logged and not retried every launch)
        log('failed: %s: %s' % (type(e).__name__, e))
        open(tried, 'w').close()
        sys.exit(1)
    finally:
        os.remove(lock)


if __name__ == '__main__':
    main()
