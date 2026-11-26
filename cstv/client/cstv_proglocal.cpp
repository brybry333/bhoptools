// The Progress Log without a server plugin: the log lives on this PC and runs are followed in the game itself, so
// the book works the same on every server (it used to need the progresslog SourceMod plugin, and each server kept its
// own history).
//
// The log: one file per map in <plat_data_dir>/progresslog/<map>.txt (attempts and spots), settings.txt for the
// book's settings. A server that still runs the plugin is ignored (its `prog_ui` lines are dropped).
//
// Runs, as shavit times them, from what this client sees:
//   - the map's timer zones: shavit's start / end boxes per track. The server's own when its bhoptools plugin
//     publishes them (they're what times you), else the public zone list
//     (https://github.com/srcwr/zones-cstrike, the list shavit servers load by default; cached for a week);
//   - the local player's position and life (entity list, netvars), and a clock of our own (seconds);
//   - leaving a start zone = a run from Start (time 0); a start zone again = restart; the end zone of the run's track
//     = finished ("<from> to End" saved when autofinish is on);
//   - a fail = dying, or being teleported to one of the map's teleport destinations (trigger_teleport targets, from
//     the BSP); the last one within 20 s is where !progsave says the attempt ended;
//   - any other teleport mid-map = a checkpoint loaded (practice): an attempt from the run time at that place, the
//     nearest point of this run's path or the longest path this map had (within 48 units). Unknown place = no attempt.
// The book talks to it with the plugin's own lines: pl_command("sm_progui ...") in, gui_prog_apply("prog_ui ...") out.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>

#include "cdll_int.h"
#include "icliententitylist.h"
#include "icliententity.h"
#include "client_class.h"
#include "dt_recv.h"
#include "networkstringtabledefs.h"

#include "cstv_gui.h"
#include "cstv_platform.h"
#include "../../client/shared/bsp_lzma.h"   // bhoptools' LZMA decoder for compressed BSP lumps

#ifdef _WIN32
static const char SEP = '\\';
#else
static const char SEP = '/';
#endif

namespace {

const int MAX_TEXT = 32;
const float FAIL_WINDOW = 20.0f;
const char *const PREFIX = " \x04[Prog]\x01 ";

IVEngineClient *g_eng;
IBaseClientDLL *g_cli;
IClientEntityList *g_ents;
INetworkStringTableContainer *g_tables;

void say_line(const std::string &line);   // the chat line, with or without its sound (the setting)

void chat(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    say_line(std::string(PREFIX) + buf);
}

// ---------------------------------------------------------------- the book's lines
void apply(const std::vector<std::string> &v)
{
    std::vector<const char *> a;
    for (const std::string &s : v) a.push_back(s.c_str());
    gui_prog_apply((int)a.size(), a.data());
}

std::string num(float v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return b; }

// ---------------------------------------------------------------- text and times (as the plugin had them)
std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// what may stay in a name (no quotes, ;, \, control characters, tabs)
std::string sanitize(const std::string &in)
{
    std::string o;
    for (char c : in)
        if ((unsigned char)c >= 0x20 && c != '"' && c != ';' && c != '\\') o += c;
    return trim(o);
}

// "1:00", "3:33.5", "1:02:03"; digits alone read like a timer: "224" = 2:24, "10203" = 1:02:03, "45" = 0:45
// ("290" isn't a time); a trailing s means seconds: "95s", "224s" = 3:44
bool parse_clock(const std::string &s0, float &out)
{
    std::string s = s0;
    bool secs = !s.empty() && (s.back() == 's' || s.back() == 'S');
    if (secs) s.pop_back();
    if (s.empty()) return false;
    int total = 0, part = 0, whole = 0, colons = 0;
    float frac = 0, scale = 0.1f;
    bool dot = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            if (dot) { frac += (c - '0') * scale; scale *= 0.1f; }
            else if (++whole > 6) return false;
            else part = part * 10 + (c - '0');
        } else if (c == ':' && !dot && whole && colons < 2) {
            total = (total + part) * 60;
            part = 0;
            whole = 0;
            colons++;
        } else if (c == '.' && !dot) dot = true;
        else return false;
    }
    if (!whole) return false;
    if (!colons && !secs && whole > 2) {
        int ss = part % 100, mm = part / 100 % 100, hh = part / 10000;
        if (ss >= 60 || (hh && mm >= 60)) return false;
        part = (hh * 60 + mm) * 60 + ss;
    }
    out = (float)(total + part) + frac;
    return true;
}

bool looks_like_time(const std::string &s)
{
    if (s.empty() || s[0] < '0' || s[0] > '9') return false;
    for (char c : s)
        if ((c < '0' || c > '9') && c != ':' && c != '.') return false;
    return true;
}

bool parse_range(const std::string &s, float &lo, float &hi)
{
    hi = -1;
    size_t dash = s.find('-');
    if (dash == std::string::npos || dash == 0) return parse_clock(trim(s), lo);
    return parse_clock(trim(s.substr(0, dash)), lo) && parse_clock(trim(s.substr(dash + 1)), hi);
}

std::string clock_text(float t)
{
    int secs = (int)floorf(t + 0.0001f);
    if (secs < 0) secs = 0;
    char b[32];
    if (secs >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", secs / 3600, secs / 60 % 60, secs % 60);
    else snprintf(b, sizeof b, "%d:%02d", secs / 60, secs % 60);
    return b;
}

std::string lower(std::string s)
{
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// ---------------------------------------------------------------- the log
struct Entry { int id; std::string day; long created; int src; std::string from, to; float ft, tt; int track; };
struct Spot { std::string name; float lo, hi; };

struct {
    bool autofinish = true, saveat_here = false;
    bool zero_clock = false;   // run time 0 (Start, a typed 0) shows as "0:00" instead of "Start"
    bool autosave = false;     // !progsave on its own each time a run from Start gets back to a start zone
    int filter = 0;            // which attempts the log page shows: 0 all, 1 runs (from Start), 2 practice
    std::string past;          // and only those that got this far: a run time or a spot's name ("" = all)
    bool chat_sound = true;    // the [Prog] chat lines play the chat sound (off: printed without it)
    int bin = 10, daystart = 5;
    bool loaded = false;
} S;

struct {
    std::string map;   // "" = not in a map
    std::vector<Entry> e;
    std::vector<Spot> s;
    int next_id = 1;
    bool unread = false, warned = false;   // its file is there but couldn't be read: never written over (until the next load)
    bool append_ok = false;                // the file ends with a whole line: a new attempt can go on its end
    std::vector<std::pair<std::string, double>> t;   // time practising per day: seconds out of the start zone, run timer going
    double t_unsaved = 0, t_saved_at = 0;
} M;

void say_line(const std::string &line)
{
    if (S.chat_sound) cstv_chat_line(line.c_str());
    else cstv_chat_quiet(line.c_str());
}

double &practised(const std::string &day)
{
    for (auto &p : M.t)
        if (p.first == day) return p.second;
    M.t.push_back({day, 0.0});
    return M.t.back().second;
}

std::string dir_path()
{
    const char *d = plat_data_dir();
    if (!*d) return "";
    std::string p = std::string(d) + SEP + "progresslog";
    plat_mkdir(p.c_str());
    return p;
}

// a map name as a file name: the BSP's name, lower case, only [a-z0-9_.-]
std::string map_file(const std::string &map)
{
    std::string d = dir_path();
    if (d.empty() || map.empty()) return "";
    std::string n;
    for (char c : map) n += isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' ? c : '_';
    return d + SEP + n + ".txt";
}

bool write_file(const std::string &path, const std::string &data)
{
    if (path.empty()) return false;
    std::string tmp = path + ".part";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fclose(f) == 0 && ok;
    return ok && plat_rename(tmp.c_str(), path.c_str());
}

bool read_file(const std::string &path, std::string &out)
{
    FILE *f = path.empty() ? nullptr : fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    out.clear();
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

std::vector<std::string> split_tabs(const std::string &line)
{
    std::vector<std::string> v;
    size_t a = 0;
    for (;;) {
        size_t b = line.find('\t', a);
        v.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v;
}

void load_settings()
{
    S.loaded = true;
    std::string d;
    if (!read_file(dir_path() + SEP + "settings.txt", d)) return;
    size_t a = 0;
    while (a < d.size()) {
        size_t b = d.find('\n', a);
        std::string line = d.substr(a, b == std::string::npos ? std::string::npos : b - a);
        a = b == std::string::npos ? d.size() : b + 1;
        if (!strncmp(line.c_str(), "past ", 5)) { S.past = sanitize(line.substr(5)).substr(0, MAX_TEXT - 1); continue; }
        char key[32];
        int v;
        if (sscanf(line.c_str(), "%31s %d", key, &v) != 2) continue;
        if (!strcmp(key, "autofinish")) S.autofinish = v != 0;
        else if (!strcmp(key, "saveat")) S.saveat_here = v != 0;
        else if (!strcmp(key, "zero")) S.zero_clock = v != 0;
        else if (!strcmp(key, "autosave")) S.autosave = v != 0;
        else if (!strcmp(key, "filter")) S.filter = v < 0 || v > 2 ? 0 : v;
        else if (!strcmp(key, "chatsound")) S.chat_sound = v != 0;
        else if (!strcmp(key, "bin")) S.bin = v < 5 ? 5 : v > 60 ? 60 : v;
        else if (!strcmp(key, "daystart")) S.daystart = v < 0 ? 0 : v > 23 ? 23 : v;
    }
}

void save_settings()
{
    char b[160];
    snprintf(b, sizeof b, "autofinish %d\nsaveat %d\nzero %d\nautosave %d\nfilter %d\nchatsound %d\nbin %d\ndaystart %d\n",
             S.autofinish, S.saveat_here, S.zero_clock, S.autosave, S.filter, S.chat_sound, S.bin, S.daystart);
    write_file(dir_path() + SEP + "settings.txt", std::string(b) + (S.past.empty() ? "" : "past " + S.past + "\n"));
}

// Format: "progresslog 1", then per attempt  e <id> <day> <created> <source> <from> <from s> <to> <to s> <track>
// and per spot  s <name> <lo> <hi>, per day  t <day> <seconds practised>  (tab-separated; -1 = no time). Source: 0 typed,
// 1 !progsave, 2 a finished run.
void load_map(const std::string &map)
{
    M.map = map;
    M.e.clear();
    M.s.clear();
    M.t.clear();
    M.t_unsaved = 0;
    M.next_id = 1;
    M.unread = M.warned = M.append_ok = false;
    std::string path = map_file(map), d;
    if (!read_file(path, d)) {
        struct stat st;
        M.unread = !path.empty() && stat(path.c_str(), &st) == 0;   // there but unreadable (locked, a disk error)
        return;
    }
    M.append_ok = !d.empty() && d.back() == '\n';
    size_t a = 0;
    while (a < d.size()) {
        size_t b = d.find('\n', a);
        std::string line = d.substr(a, b == std::string::npos ? std::string::npos : b - a);
        a = b == std::string::npos ? d.size() : b + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f = split_tabs(line);
        if (f[0] == "e" && f.size() >= 10) {
            Entry e = {atoi(f[1].c_str()), f[2], atol(f[3].c_str()), atoi(f[4].c_str()), f[5], f[7],
                       (float)atof(f[6].c_str()), (float)atof(f[8].c_str()), atoi(f[9].c_str())};
            if (e.id <= 0) continue;
            M.e.push_back(e);
            M.next_id = std::max(M.next_id, e.id + 1);
        } else if (f[0] == "s" && f.size() >= 4) {
            M.s.push_back({f[1], (float)atof(f[2].c_str()), (float)atof(f[3].c_str())});
        } else if (f[0] == "t" && f.size() >= 3 && !f[1].empty()) {
            double v = atof(f[2].c_str());
            if (v > 0 && v < 1e9) practised(f[1]) += v;
        }
    }
    std::sort(M.e.begin(), M.e.end(), [](const Entry &x, const Entry &y) { return x.id < y.id; });
}

std::string entry_line(const Entry &e)
{
    char b[512];
    snprintf(b, sizeof b, "e\t%d\t%s\t%ld\t%d\t%s\t%.3f\t%s\t%.3f\t%d\n", e.id, e.day.c_str(), e.created, e.src, e.from.c_str(), e.ft,
             e.to.c_str(), e.tt, e.track);
    return b;
}

void save_map()
{
    if (M.map.empty()) return;
    if (M.unread) {
        if (!M.warned) {
            M.warned = true;
            chat("This map's Progress Log file couldn't be read (%s), so nothing is saved to it (that would wipe it). Rejoin the "
                 "map to try again.", map_file(M.map).c_str());
        }
        return;
    }
    std::string d = "progresslog 1\n";
    char b[512];
    for (const Entry &e : M.e) d += entry_line(e);
    for (const Spot &s : M.s) {
        snprintf(b, sizeof b, "s\t%s\t%.3f\t%.3f\n", s.name.c_str(), s.lo, s.hi);
        d += b;
    }
    for (const auto &p : M.t) {
        snprintf(b, sizeof b, "t\t%s\t%.1f\n", p.first.c_str(), p.second);
        d += b;
    }
    M.t_unsaved = 0;
    M.t_saved_at = plat_now();
    if (!write_file(map_file(M.map), d)) chat("Couldn't save the Progress Log (%s).", map_file(M.map).c_str());
    else M.append_ok = true;
}

// a new attempt: one line on the end of the file (changes and deletes rewrite it)
void save_new_entry(const Entry &e)
{
    std::string path = M.append_ok && !M.unread ? map_file(M.map) : "";
    FILE *f = path.empty() ? nullptr : fopen(path.c_str(), "ab");
    bool ok = f && fputs(entry_line(e).c_str(), f) >= 0;
    if (f) ok = fclose(f) == 0 && ok;
    if (!ok) {   // the whole file instead (that also cleans up a half-written line)
        M.append_ok = false;
        save_map();
    }
}

// the log's day for a time: before the day start hour counts as the day before
std::string day_of(time_t t)
{
    t -= (time_t)S.daystart * 3600;
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[16];
    strftime(b, sizeof b, "%Y-%m-%d", &tmv);
    return b;
}

Spot *find_spot(const std::string &name)
{
    for (Spot &s : M.s)
        if (!strcasecmp(s.name.c_str(), name.c_str())) return &s;
    return nullptr;
}

float spot_middle(float lo, float hi) { return hi > lo ? (lo + hi) * 0.5f : lo; }

float spot_time(const std::string &name)
{
    Spot *s = find_spot(name);
    return s ? spot_middle(s->lo, s->hi) : -1.0f;
}

float end_time() { return spot_time("End"); }

// ---------------------------------------------------------------- telling the book
void send_entry(const Entry &e)
{
    apply({"prog_ui", "e", std::to_string(e.id), e.day, std::to_string(e.created), std::to_string(e.src), e.from, num(e.ft), e.to, num(e.tt)});
}

void send_cfg()
{
    apply({"prog_ui", "cfg", S.autofinish ? "1" : "0", S.saveat_here ? "1" : "0", std::to_string(S.daystart), day_of(time(nullptr)), std::to_string(S.bin),
           S.zero_clock ? "1" : "0", S.autosave ? "1" : "0", std::to_string(S.filter), S.past,
           S.chat_sound ? "1" : "0"});
}

void sync_book()
{
    apply({"prog_ui", "reset", M.map, day_of(time(nullptr)), std::to_string(S.daystart), S.autofinish ? "1" : "0", S.saveat_here ? "1" : "0",
           std::to_string(S.bin), S.zero_clock ? "1" : "0", S.autosave ? "1" : "0", std::to_string(S.filter), S.past,
           S.chat_sound ? "1" : "0"});
    for (const Entry &e : M.e) send_entry(e);
    for (const Spot &s : M.s) apply({"prog_ui", "s", s.name, num(s.lo), num(s.hi)});
    for (const auto &p : M.t) apply({"prog_ui", "t", p.first, std::to_string((long)p.second)});
    apply({"prog_ui", "done"});
}

// ---------------------------------------------------------------- changing it
// what someone typed for one end: Start, End, a run time (2:24, 224, 3:33.5, 1:02:03, 95s) or a spot's name
bool parse_point(const std::string &input, std::string &text, float &time)
{
    std::string s = sanitize(input);
    time = -1;
    if (s.empty()) return false;
    if (!strcasecmp(s.c_str(), "start")) { text = "Start"; time = 0; return true; }
    float t;
    if (parse_clock(s, t)) { time = t; text = clock_text(t); return true; }
    if (looks_like_time(s)) return false;
    text = !strcasecmp(s.c_str(), "end") ? "End" : s.substr(0, MAX_TEXT - 1);
    time = spot_time(text);
    return true;
}

// run time 0: Start, or a typed 0 / 0:00
bool zero_point(const std::string &text)
{
    float t;
    return !strcasecmp(text.c_str(), "start") || (looks_like_time(text) && parse_clock(text, t) && t < 0.001f);
}

// "0:22 (8%)", "Start" (or "0:00", the setting), "End", "End slope"
std::string point_label(const std::string &text, float time)
{
    if (zero_point(text)) return S.zero_clock ? "0:00" : "Start";
    float end = end_time(), t;
    if (end > 0 && time >= 0 && parse_clock(text, t)) {
        char b[64];
        snprintf(b, sizeof b, "%s (%.0f%%)", text.c_str(), std::min(time / end * 100.0f, 100.0f));
        return b;
    }
    return text;
}

void print_saved(const std::string &from, float ft, const std::string &to, float tt)
{
    chat("Saved %s to %s.", point_label(from, ft).c_str(), point_label(to, tt).c_str());
}

int insert_entry(const std::string &from, float ft, const std::string &to, float tt, int src, int track)
{
    if (M.map.empty()) { chat("Join a map first."); return 0; }
    time_t now = time(nullptr);
    Entry e = {M.next_id++, day_of(now), (long)now, src, from, to, ft, tt, track};
    M.e.push_back(e);
    save_new_entry(e);
    send_entry(e);
    return e.id;
}

Entry *find_entry(int id)
{
    for (Entry &e : M.e)
        if (e.id == id) return &e;
    return nullptr;
}

bool add_points(const std::string &a, const std::string &b)
{
    std::string ft, tt;
    float f, t;
    if (!parse_point(a, ft, f) || !parse_point(b, tt, t)) return false;
    if (insert_entry(ft, f, tt, t, 0, 0) > 0) print_saved(ft, f, tt, t);
    return true;
}

bool add_typed(const std::string &input)
{
    static const char *const seps[] = {" to ", "->", " - "};
    for (const char *sep : seps) {
        std::string li = lower(input);
        size_t at = li.find(sep);
        if (at == std::string::npos || at == 0) continue;
        if (add_points(input.substr(0, at), input.substr(at + strlen(sep)))) return true;
        break;
    }
    chat("Like \x04" "2:24 to 4:02\x01, \x04" "224 to 402\x01 or \x04Start to End slope\x01.");
    return false;
}

void set_spot(const std::string &input, float lo, float hi)
{
    std::string name = sanitize(input).substr(0, MAX_TEXT - 1);
    if (name.empty() || !strcasecmp(name.c_str(), "start") || lo < 0 || M.map.empty()) return;
    if (!strcasecmp(name.c_str(), "end")) { name = "End"; hi = -1; }
    if (hi >= 0 && hi < lo) std::swap(lo, hi);
    if (hi <= lo) hi = -1;
    if (Spot *s = find_spot(name)) *s = {name, lo, hi};
    else M.s.push_back({name, lo, hi});
    if (name != "End") {   // (finished runs keep their own time)
        float mid = spot_middle(lo, hi);
        for (Entry &e : M.e) {
            bool ch = false;
            if (!strcasecmp(e.from.c_str(), name.c_str())) { e.ft = mid; ch = true; }
            if (!strcasecmp(e.to.c_str(), name.c_str())) { e.tt = mid; ch = true; }
            if (ch) send_entry(e);
        }
    }
    save_map();
    apply({"prog_ui", "s", name, num(lo), num(hi)});
    if (hi >= 0) chat("Saved %s at %s-%s.", name.c_str(), clock_text(lo).c_str(), clock_text(hi).c_str());
    else chat("Saved %s at %s.", name.c_str(), clock_text(lo).c_str());
}

// a spot's new name (the book's spot form): the log's attempts at it get it too. Start and End aren't renamed.
void rename_spot(const std::string &old_in, const std::string &new_in)
{
    std::string to = sanitize(new_in).substr(0, MAX_TEXT - 1);
    Spot *s = find_spot(sanitize(old_in));
    if (!s || to.empty() || s->name == to || !strcasecmp(to.c_str(), "start") || !strcasecmp(to.c_str(), "end")
        || !strcasecmp(s->name.c_str(), "end"))
        return;
    std::string was = s->name;
    Spot *taken = find_spot(to);
    if (taken && taken != s) M.s.erase(M.s.begin() + (s - M.s.data()));   // that spot stays (the spot line after this sets it)
    else s->name = to;
    for (Entry &e : M.e) {
        bool ch = false;
        if (!strcasecmp(e.from.c_str(), was.c_str())) { e.from = to; ch = true; }
        if (!strcasecmp(e.to.c_str(), was.c_str())) { e.to = to; ch = true; }
        if (ch) send_entry(e);
    }
    save_map();
    apply({"prog_ui", "sd", was});
    if (Spot *n = find_spot(to)) apply({"prog_ui", "s", n->name, num(n->lo), num(n->hi)});
    if (!strcasecmp(S.past.c_str(), was.c_str())) {   // the log's "past" filter named it: it follows
        S.past = to;
        save_settings();
        send_cfg();
    }
}

void delete_spot(const std::string &input)
{
    std::string name = sanitize(input);
    size_t n = M.s.size();
    M.s.erase(std::remove_if(M.s.begin(), M.s.end(), [&](const Spot &s) { return !strcasecmp(s.name.c_str(), name.c_str()); }), M.s.end());
    if (M.s.size() != n) save_map();
    apply({"prog_ui", "sd", name});
}

// ---------------------------------------------------------------- the map: zones, teleport destinations
struct Zone { int type, track; float lo[3], hi[3]; };   // type 0 start, 1 end
struct Vec3 { float v[3]; };

std::mutex g_zmu;
std::vector<Zone> g_zones;          // this map's (guarded by g_zmu: the fetch thread fills it)
std::string g_zone_map;             // the map they're for
std::string g_zone_status = "none"; // for pl_debug
bool g_zones_server;                // g_zones are the server's: the zone list's answers are ignored
std::string g_srv_header;           // the server's zone list last read (its header changes with the list)
double g_srv_polled = 0;
std::vector<Vec3> g_dests;          // where the map's trigger_teleports send you

// a minimal JSON reader: enough for the zone list
struct J {
    enum { NUL, NUM, STR, ARR, OBJ } t = NUL;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;
    const J *get(const char *k) const
    {
        for (auto &kv : o)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

void jws(const char *&p) { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; }

bool jparse(const char *&p, J &v, int depth)
{
    if (depth > 32) return false;
    jws(p);
    if (*p == '{' || *p == '[') {
        bool obj = *p++ == '{';
        v.t = obj ? J::OBJ : J::ARR;
        jws(p);
        if (*p == (obj ? '}' : ']')) { p++; return true; }
        for (;;) {
            std::string key;
            if (obj) {
                J k;
                if (!jparse(p, k, depth + 1) || k.t != J::STR) return false;
                jws(p);
                if (*p++ != ':') return false;
                key = k.s;
            }
            J x;
            if (!jparse(p, x, depth + 1)) return false;
            if (obj) v.o.push_back({key, std::move(x)});
            else v.a.push_back(std::move(x));
            jws(p);
            if (*p == ',') { p++; continue; }
            if (*p == (obj ? '}' : ']')) { p++; return true; }
            return false;
        }
    }
    if (*p == '"') {
        v.t = J::STR;
        for (p++; *p && *p != '"'; p++) {
            if (*p == '\\' && p[1]) {
                p++;
                if (*p == 'u') { v.s += '?'; for (int i = 0; i < 4 && p[1]; i++) p++; }
                else v.s += *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
            } else v.s += *p;
        }
        if (*p != '"') return false;
        p++;
        return true;
    }
    for (const char *w : {"true", "false", "null"})
        if (!strncmp(p, w, strlen(w))) { p += strlen(w); v.t = J::NUL; return true; }
    char *e;
    v.n = strtod(p, &e);
    if (e == p) return false;
    v.t = J::NUM;
    p = e;
    return true;
}

// shavit's json zones ([{"type": "start", "track": 0, "point_a": [x, y, z], "point_b": [...]}, ...]): start and end only
bool parse_zones(const char *text, std::vector<Zone> &out)
{
    J root;
    const char *p = text;
    if (!jparse(p, root, 0) || root.t != J::ARR) return false;
    for (const J &z : root.a) {
        const J *type = z.get("type"), *track = z.get("track"), *a = z.get("point_a"), *b = z.get("point_b");
        if (!type || type->t != J::STR || !a || !b || a->t != J::ARR || b->t != J::ARR || a->a.size() < 3 || b->a.size() < 3) continue;
        int t = type->s == "start" ? 0 : type->s == "end" ? 1 : -1;
        if (t < 0) continue;
        Zone zone = {t, track && track->t == J::NUM ? (int)track->n : 0, {}, {}};
        for (int i = 0; i < 3; i++) {
            float x = (float)a->a[i].n, y = (float)b->a[i].n;
            zone.lo[i] = std::min(x, y);
            zone.hi[i] = std::max(x, y);
        }
        out.push_back(zone);
    }
    return true;
}

std::string zone_cache(const std::string &map)
{
    std::string d = dir_path();
    if (d.empty()) return "";
    d += SEP;
    d += "zones";
    plat_mkdir(d.c_str());
    return d + SEP + map + ".json";
}

// The zones a server sent for the map (its bhoptools plugin), kept in the zone list's json form: used on servers that
// don't send theirs when the zone list hasn't got the map.
std::string saved_zones_path(const std::string &map)
{
    std::string c = zone_cache(map);
    return c.empty() ? "" : c.substr(0, c.size() - 5) + ".server.json";
}

void save_server_zones(const std::string &map, const std::vector<Zone> &z)
{
    std::string text = "[", old;
    char b[256];
    for (const Zone &q : z) {
        snprintf(b, sizeof b, "%s{\"type\":\"%s\",\"track\":%d,\"point_a\":[%.3f,%.3f,%.3f],\"point_b\":[%.3f,%.3f,%.3f]}",
                 text.size() > 1 ? "," : "", q.type ? "end" : "start", q.track, q.lo[0], q.lo[1], q.lo[2], q.hi[0], q.hi[1], q.hi[2]);
        text += b;
    }
    text += "]";
    std::string path = saved_zones_path(map);
    if (!read_file(path, old) || old != text) write_file(path, text);
}

time_t file_age(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return (time_t)-1;
    return time(nullptr) - st.st_mtime;
}

std::string g_fetch_map;   // handed to the thread (set before it starts)

void zones_ready(const std::string &map, std::vector<Zone> z, const char *status)
{
    std::string why = status, text;
    if (z.empty() && read_file(saved_zones_path(map), text) && parse_zones(text.c_str(), z) && !z.empty())
        why += "; using the zones a server sent before";
    std::lock_guard<std::mutex> l(g_zmu);
    if (map != g_zone_map || g_zones_server) return;   // the map changed meanwhile, or the server sent its own
    g_zones = std::move(z);
    char b[192];
    int st = 0, en = 0;
    for (const Zone &q : g_zones) (q.type ? en : st)++;
    snprintf(b, sizeof b, "%s: %d start, %d end", why.c_str(), st, en);
    g_zone_status = b;
}

void *fetch_thread(void *)
{
    std::string map = g_fetch_map, cache = zone_cache(map);
    std::string url = "https://zones-cstrike.srcwr.com/z/" + map + ".json";   // (its github.io address redirects there over http)
    size_t len = 0;
    int status = 0;
    char *body = plat_http_get(url.c_str(), &len, &status);
    std::vector<Zone> z;
    if (body && parse_zones(body, z)) {
        write_file(cache, std::string(body, len));
        zones_ready(map, z, "zone list (downloaded)");
    } else if (status == 404) {
        write_file(cache, "[]");   // not in the list: asked again in a week
        zones_ready(map, z, "no zones for this map in the zone list");
    } else {   // no good answer: the zones the cache gave stay
        const char *why = status ? "zone list: bad answer" : "zone list: no connection";
        bool none = false;
        {
            std::lock_guard<std::mutex> l(g_zmu);
            if (map == g_zone_map && !g_zones_server) {
                none = g_zones.empty();
                g_zone_status = std::string(why) + ", kept " + std::to_string(g_zones.size()) + " cached zones";
            }
        }
        if (none) zones_ready(map, z, why);   // nothing cached: the zones a server sent before, if any
    }
    free(body);
    return nullptr;
}

void load_zones(const std::string &map)
{
    {
        std::lock_guard<std::mutex> l(g_zmu);
        g_zone_map = map;
        g_zones.clear();
        g_zones_server = false;
        g_zone_status = "loading";
    }
    g_srv_header.clear();
    g_srv_polled = 0;
    std::string cache = zone_cache(map), text;
    time_t age = file_age(cache);
    std::vector<Zone> z;
    if (age >= 0 && read_file(cache, text) && parse_zones(text.c_str(), z)) {
        zones_ready(map, z, "zone list (cached)");
        if (age < 7 * 86400) return;
    }
    g_fetch_map = map;
    plat_http_init();
    if (!plat_thread(fetch_thread)) zones_ready(map, z, "zone list: no thread");
}

// The server's own zones: its bhoptools plugin publishes shavit's zone list in the InfoPanel string table
// (bhoptools/zones.sp + tables.sp): "bhoptools_zones" = "<parts> <crc32> <format> <list number>", the text in
// "bhoptools_zones_0".. One zone a line: type track form data hammerid origin[3] half[3] dest[3], floats as their bits
// in hex (SourcePawn's %x: a negative cell gets a minus sign). Read like bhoptools' prediction reads it (PollZones).
std::string table_data(INetworkStringTable *t, const char *name)
{
    int i = t->FindStringIndex(name);
    if (i < 0 || i == INVALID_STRING_INDEX) return "";
    int len = 0;
    const char *p = (const char *)t->GetStringUserData(i, &len);
    return p && len > 0 ? std::string(p, (size_t)len) : "";
}

uint32_t crc32_of(const std::string &s)   // zlib's, as the plugin takes it
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) {
        crc ^= c;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

float hex_float(const char *s)
{
    bool neg = *s == '-';
    if (neg) s++;
    uint32_t bits = 0;
    for (;; s++) {
        int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 : *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : -1;
        if (d < 0) break;
        bits = bits << 4 | (uint32_t)d;
    }
    if (neg) bits = 0u - bits;
    float f;
    memcpy(&f, &bits, 4);
    return std::isfinite(f) && fabsf(f) < 1e6f ? f : 0.0f;
}

void poll_server_zones()
{
    double now = plat_now();
    if (!g_tables || M.map.empty() || now - g_srv_polled < 0.5) return;
    g_srv_polled = now;
    INetworkStringTable *t = g_tables->FindTable("InfoPanel");
    std::string head = t ? table_data(t, "bhoptools_zones") : "";
    if (head.empty() || head == g_srv_header) return;
    int parts = 0, crc = 0, used = 0, format = 0, list = 0;
    if (sscanf(head.c_str(), "%d %d %n", &parts, &crc, &used) != 2 || sscanf(head.c_str() + used, "%d %d", &format, &list) != 2
        || format != 1 || parts < 0 || parts > 4096)
        return;
    std::string text;
    for (int i = 0; i < parts; i++) {
        std::string part = table_data(t, ("bhoptools_zones_" + std::to_string(i)).c_str());
        if (part.empty()) return;   // not all here yet: read again next time
        text += part;
    }
    if ((int)crc32_of(text) != crc) return;
    g_srv_header = head;
    std::vector<Zone> z;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::vector<std::string> f;
        for (size_t a = p; a < e;) {
            size_t b = text.find(' ', a);
            if (b == std::string::npos || b > e) b = e;
            if (b > a) f.push_back(text.substr(a, b - a));
            a = b + 1;
        }
        p = e + 1;
        if (f.size() < 14) continue;
        int type = atoi(f[0].c_str()), form = atoi(f[2].c_str());
        if ((type != 0 && type != 1) || form > 2) continue;   // start / end; boxes and hooked triggers, not buttons
        Zone zone = {type, atoi(f[1].c_str()), {}, {}};
        for (int i = 0; i < 3; i++) {   // the box as bhoptools touches it: a unit wider (ZsBoxTouches)
            float o = hex_float(f[5 + i].c_str()), h = hex_float(f[8 + i].c_str());
            zone.lo[i] = o - h - 1;
            zone.hi[i] = o + h + 1;
        }
        z.push_back(zone);
    }
    // an empty list (published before shavit loaded the map's zones) doesn't replace the zone list's
    if (z.empty()) return;
    save_server_zones(M.map, z);   // for servers that don't send theirs
    std::lock_guard<std::mutex> l(g_zmu);
    g_zones_server = true;
    g_zones = std::move(z);
    int st = 0, en = 0;
    for (const Zone &q : g_zones) (q.type ? en : st)++;
    char b[96];
    snprintf(b, sizeof b, "the server's zones (list %d): %d start, %d end", list, st, en);
    g_zone_status = b;
}

// the BSP's entity lump: where trigger_teleports send you (their target's origin)
void load_dests(const std::string &level)
{
    g_dests.clear();
    FILE *f = nullptr;
    for (const char *pre : {"cstrike/", "cstrike/download/"})
        if ((f = fopen((std::string(pre) + level).c_str(), "rb"))) break;
    if (!f) return;
    int32_t head[2 + 64 * 4];
    std::string ents;
    if (fread(head, 1, sizeof head, f) == sizeof head && !memcmp(head, "VBSP", 4)) {
        int32_t off = head[2], len = head[3];
        if (off > 0 && len > 0 && len < (64 << 20) && fseek(f, off, SEEK_SET) == 0) {
            ents.resize((size_t)len);
            if (fread(&ents[0], 1, ents.size(), f) != ents.size()) ents.clear();
        }
    }
    fclose(f);
    if (ents.compare(0, 4, "LZMA") == 0) {   // a compressed lump (bhop_lust, bhop_saturn, ...): bhoptools' decoder
        std::vector<uint8_t> raw;
        if (!bsplzma::DecodeLump((const uint8_t *)ents.data(), ents.size(), raw)) return;
        ents.assign((const char *)raw.data(), raw.size());
    }
    struct Ent { std::string cls, name, target; Vec3 o; bool has_o; };
    std::vector<Ent> list;
    size_t p = 0;
    while ((p = ents.find('{', p)) != std::string::npos) {
        size_t e = ents.find('}', p);
        if (e == std::string::npos) break;
        Ent en = {"", "", "", {{0, 0, 0}}, false};
        size_t q = p + 1;
        for (;;) {
            size_t k1 = ents.find('"', q);
            if (k1 == std::string::npos || k1 > e) break;
            size_t k2 = ents.find('"', k1 + 1), v1 = k2 == std::string::npos ? k2 : ents.find('"', k2 + 1);
            size_t v2 = v1 == std::string::npos ? v1 : ents.find('"', v1 + 1);
            if (v2 == std::string::npos || v2 > e) break;
            std::string k = ents.substr(k1 + 1, k2 - k1 - 1), v = ents.substr(v1 + 1, v2 - v1 - 1);
            if (k == "classname") en.cls = v;
            else if (k == "targetname") en.name = v;
            else if (k == "target") en.target = v;
            else if (k == "origin") en.has_o = sscanf(v.c_str(), "%f %f %f", &en.o.v[0], &en.o.v[1], &en.o.v[2]) == 3;
            q = v2 + 1;
        }
        list.push_back(en);
        p = e + 1;
    }
    for (const Ent &t : list) {
        if (t.cls != "trigger_teleport" || t.target.empty()) continue;
        for (const Ent &d : list)
            if (d.has_o && !strcasecmp(d.name.c_str(), t.target.c_str())) { g_dests.push_back(d.o); break; }
    }
}

bool near_dest(const float *p)
{
    for (const Vec3 &d : g_dests)
        if (fabsf(p[0] - d.v[0]) < 64 && fabsf(p[1] - d.v[1]) < 64 && fabsf(p[2] - d.v[2]) < 96) return true;
    return false;
}

// ---------------------------------------------------------------- the local player
struct {
    bool tried = false;
    int life = -1, flags = -1, vel = -1;   // netvar offsets in the player (-1 = not found)
} NV;

int find_prop(RecvTable *t, const char *name, int depth)
{
    if (!t || depth > 10) return -1;
    for (int i = 0; i < t->m_nProps; i++) {
        RecvProp *p = &t->m_pProps[i];
        if (p->m_pVarName && !strcmp(p->m_pVarName, name)) return p->GetOffset();
        if (p->GetType() == DPT_DataTable && p->GetDataTable()) {
            int o = find_prop(p->GetDataTable(), name, depth + 1);
            if (o >= 0) return o + p->GetOffset();
        }
    }
    return -1;
}

void find_netvars()
{
    NV.tried = true;
    for (ClientClass *c = g_cli ? g_cli->GetAllClasses() : nullptr; c; c = c->m_pNext) {
        if (!c->m_pNetworkName || strcmp(c->m_pNetworkName, "CCSPlayer")) continue;
        NV.life = find_prop(c->m_pRecvTable, "m_lifeState", 0);
        NV.flags = find_prop(c->m_pRecvTable, "m_fFlags", 0);
        NV.vel = find_prop(c->m_pRecvTable, "m_vecVelocity[0]", 0);
        break;
    }
}

struct Me { float pos[3], vel[3]; bool alive, ducked; };

bool read_me(Me &me)
{
    if (!g_eng || !g_ents || !g_eng->IsInGame() || g_eng->IsPlayingDemo()) return false;   // (a demo's player isn't you)
    if (!NV.tried) find_netvars();
    IClientEntity *ent = g_ents->GetClientEntity(g_eng->GetLocalPlayer());
    if (!ent) return false;
    const Vector &o = ent->GetAbsOrigin();
    me.pos[0] = o.x; me.pos[1] = o.y; me.pos[2] = o.z;
    const char *base = (const char *)ent;
    me.alive = NV.life < 0 || base[NV.life] == 0;   // LIFE_ALIVE
    me.ducked = NV.flags >= 0 && (*(const int *)(base + NV.flags) & 2);   // FL_DUCKING
    for (int i = 0; i < 3; i++) me.vel[i] = NV.vel >= 0 ? ((const float *)(base + NV.vel))[i] : 0;
    return true;
}

// ---------------------------------------------------------------- the run
struct Sample { float p[3]; float t; };
enum { R_NONE, R_IN_START, R_RUN, R_DONE };

struct {
    int state = R_NONE, track = 0;
    double t0 = 0;                // plat_now() at run time 0
    bool att = false;             // an attempt is going
    std::string from;
    float ft = -1;
    double att_at = 0;
    int att_entry = 0;            // the entry !progsave saved for it (again = updates it)
    float fail_time = -1;
    double fail_at = 0;
    bool have = false, alive = true;
    float last[3] = {0, 0, 0}, last_time = -1;
    double last_at = 0, sample_at = 0, live_at = 0;
    std::vector<Sample> trace, best;   // this run's path; the longest one this map had
    std::string lost;             // why the last teleport started no attempt
    // the last attempt that got back to a start zone (a restart, a one-shot map's fail teleport, a respawn, walking
    // back): !progsave in the start zone saves it, to its run time just before. A finished run clears it.
    bool ended = false;
    std::string ended_from;
    float ended_ft = -1, ended_tt = -1;
    int ended_track = 0, ended_entry = 0;
} R;

bool g_viewing;

float run_time(double now) { return R.state == R_RUN ? (float)(now - R.t0) : -1.0f; }

void begin_attempt(const std::string &from, float ft, double now)
{
    R.att = true;
    R.from = from;
    R.ft = ft;
    R.att_at = now;
    R.att_entry = 0;
    R.fail_at = 0;
    R.lost.clear();
}

void keep_trace()
{
    float a = R.trace.empty() ? 0 : R.trace.back().t, b = R.best.empty() ? 0 : R.best.back().t;
    if (a > b) R.best.swap(R.trace);
    R.trace.clear();
}

// the run time at a place: the nearest point of this run's path or the longest one (within 48 units), else -1
float time_at(const float *p)
{
    float best_d = 48 * 48, t = -1;
    for (const std::vector<Sample> *tr : {&R.trace, &R.best})
        for (size_t i = 0; i + 1 < tr->size(); i++) {
            const Sample &a = (*tr)[i], &b = (*tr)[i + 1];
            float ab[3], ap[3], l2 = 0, dot = 0;
            for (int k = 0; k < 3; k++) { ab[k] = b.p[k] - a.p[k]; ap[k] = p[k] - a.p[k]; l2 += ab[k] * ab[k]; dot += ab[k] * ap[k]; }
            float u = l2 > 0 ? std::max(0.0f, std::min(1.0f, dot / l2)) : 0, d = 0;
            for (int k = 0; k < 3; k++) { float x = ap[k] - u * ab[k]; d += x * x; }
            if (d < best_d) { best_d = d; t = a.t + u * (b.t - a.t); }
        }
    return t;
}

// in a zone of this type: which track (-1 none). The player's box: 32 x 32, 72 high (54 ducked).
int zone_at(const float *p, bool ducked, int type)
{
    std::lock_guard<std::mutex> l(g_zmu);
    float lo[3] = {p[0] - 16, p[1] - 16, p[2]}, hi[3] = {p[0] + 16, p[1] + 16, p[2] + (ducked ? 54.0f : 72.0f)};
    for (const Zone &z : g_zones)
        if (z.type == type && lo[0] <= z.hi[0] && hi[0] >= z.lo[0] && lo[1] <= z.hi[1] && hi[1] >= z.lo[1] && lo[2] <= z.hi[2] && hi[2] >= z.lo[2])
            return z.track;
    return -1;
}

bool have_zones()
{
    std::lock_guard<std::mutex> l(g_zmu);
    return !g_zones.empty();
}

// where the attempt ended: the last fail if it was recent, else where you are
float attempt_end(double now)
{
    if (!S.saveat_here && R.fail_at > R.att_at && now - R.fail_at < FAIL_WINDOW) return R.fail_time;
    return run_time(now);
}

// !progsave's entry for an attempt: a new one, or the one it saved before (entry) changed to the new end
void save_attempt(int &entry, const std::string &from, float ft, float tt, int track)
{
    std::string to = clock_text(tt);
    Entry *e = entry ? find_entry(entry) : nullptr;
    if (e) {
        e->to = to;
        e->tt = tt;
        e->src = 1;
        save_map();
        send_entry(*e);
    } else {
        entry = insert_entry(from, ft, to, tt, 1, track);
        if (!entry) return;
    }
    print_saved(from, ft, to, tt);
}

void progsave()
{
    double now = plat_now();
    bool running = R.att && R.state == R_RUN;
    // in the start zone: the attempt that just ended there (a restart, a one-shot map's fail teleport, a respawn)
    if (R.state == R_IN_START && R.ended) {
        save_attempt(R.ended_entry, R.ended_from, R.ended_ft, R.ended_tt, R.ended_track);
        return;
    }
    if (!running) {
        if (!have_zones()) chat("This map's timer zones aren't known (the server doesn't send them and the public zone list hasn't got this map), so runs can't be followed here. Type them: \x04!prog 2:24 to 4:02\x01.");
        else if (!R.lost.empty()) chat("%s", R.lost.c_str());
        else chat("Start a run or load a checkpoint first.");
        return;
    }
    save_attempt(R.att_entry, R.from, R.ft, attempt_end(now), R.track);
}

void finish(double now)
{
    float t = run_time(now);
    // (an attempt that began this moment was a teleport into the end zone, like shavit's !end: not a run)
    if (R.att && S.autofinish && now - R.att_at >= 0.1 && insert_entry(R.from, R.ft, "End", t, 2, R.track) > 0) print_saved(R.from, R.ft, "End", t);
    R.att = false;
    R.ended = false;
    R.state = R_DONE;
    keep_trace();
}

void tick()
{
    if (M.map.empty()) return;
    Me me;
    if (!read_me(me)) return;
    double now = plat_now();
    if (!me.alive) {
        if (R.alive && R.state == R_RUN && R.att) { R.fail_time = run_time(now); R.fail_at = now; }
        R.alive = false;
        R.have = false;
        return;
    }
    R.alive = true;
    bool tele = false;
    float d2 = 0;
    if (R.have) {
        float v2 = 0;
        // (speed as |x| + |y| + |z|, a bit over the real one: no sqrtf, a glibc 2.43 symbol here, too new for Steam's runtime)
        for (int i = 0; i < 3; i++) { float x = me.pos[i] - R.last[i]; d2 += x * x; v2 += fabsf(me.vel[i]); }
        // a teleport: farther than twice your speed covers in a tenth of a second (at least). A booster pushes you
        // on the server only, and the prediction correction moves you ~100 units at once: that isn't one.
        float dt = (float)std::min(now - R.last_at, 0.25), reach = v2 * std::max(dt, 0.1f) * 2 + 64 + 3500 * dt * (NV.vel < 0);
        tele = d2 > reach * reach;
    }
    int start = zone_at(me.pos, me.ducked, 0);
    if (start >= 0) {
        // back in a start zone mid-attempt (a restart, a one-shot map's fail teleport, a respawn, walking back): the
        // attempt ends here, at its run time just before ("last fail": a fail it had in the last 20 s, as a restart
        // after falling somewhere else). !progsave in the start zone saves it.
        bool just_ended = R.state == R_RUN && R.att && R.last_time >= 0;
        if (just_ended) {
            bool failed = !S.saveat_here && R.fail_at > R.att_at && now - R.fail_at < FAIL_WINDOW;
            R.ended = true;
            R.ended_from = R.from;
            R.ended_ft = R.ft;
            R.ended_tt = failed ? R.fail_time : R.last_time;
            R.ended_track = R.track;
            R.ended_entry = R.att_entry;
        }
        if (R.state == R_RUN) keep_trace();
        R.state = R_IN_START;
        R.track = start;
        R.att = false;
        R.lost.clear();
        // "Auto save": what !progsave in the start zone does, on its own, for runs from the start zone only (practice
        // from a checkpoint is never saved by it; !progsave still saves it)
        if (just_ended && S.autosave && R.ended_from == "Start")
            save_attempt(R.ended_entry, R.ended_from, R.ended_ft, R.ended_tt, R.ended_track);
    } else if (R.state == R_IN_START && !tele) {   // left the start zone: the run starts
        R.state = R_RUN;
        R.t0 = now;
        R.trace.clear();
        begin_attempt("Start", 0, now);
    } else if (tele && (R.state == R_RUN || R.state == R_DONE || R.state == R_IN_START)) {
        if (near_dest(me.pos)) {   // the map sent you back: a fail (the attempt goes on, as shavit's timer does)
            if (R.state == R_RUN && R.att) { R.fail_time = R.last_time; R.fail_at = now; }
        } else {                   // a checkpoint loaded: an attempt from your time there
            float t = time_at(me.pos);
            bool running = R.state == R_RUN && R.att;
            if (running && (t < 0 ? d2 < 1024.0f * 1024.0f : fabsf(t - run_time(now)) < 1.0f)) {
                // a short jump to nowhere known, or onto this run's own time: the run goes on (as shavit's timer does)
            } else if (t >= 0) {
                R.state = R_RUN;
                R.t0 = now - t;
                begin_attempt(clock_text(t), t, now);
            } else {
                if (R.state == R_RUN) keep_trace();
                R.state = R_NONE;
                R.att = false;
                R.lost = "Teleported somewhere this map's runs never went, so there's no run time here. Finish a run "
                         "past this place first (or type the attempt: !prog 2:24 to 4:02).";
            }
        }
    }
    if (R.state == R_RUN) {
        int end = zone_at(me.pos, me.ducked, 1);
        if (end >= 0 && end == R.track) finish(now);
    }
    if (R.state == R_RUN && now - R.sample_at >= 1.0 / 60 && R.trace.size() < 60 * 60 * 60) {
        R.sample_at = now;
        Sample s = {{me.pos[0], me.pos[1], me.pos[2]}, run_time(now)};
        R.trace.push_back(s);
    }
    // time practising: out of the start zone with the run timer going (not dead, not finished, not in a start zone)
    static std::string today;
    static double today_at = -1e9, t_sent_at = 0;
    if (now - today_at >= 30) { today = day_of(time(nullptr)); today_at = now; }
    if (R.state == R_RUN && R.have) {
        double dt = std::min(now - R.last_at, 0.25);
        practised(today) += dt;
        M.t_unsaved += dt;
        if (now - M.t_saved_at >= 60) save_map();
    }
    if (g_viewing && now - t_sent_at >= 1) {
        t_sent_at = now;
        apply({"prog_ui", "t", today, std::to_string((long)practised(today))});
    }
    if (g_viewing && now - R.live_at >= 0.25) {
        R.live_at = now;
        if (R.att && R.state == R_RUN) {
            float fail = R.fail_at > R.att_at && now - R.fail_at < FAIL_WINDOW ? R.fail_time : -1;
            apply({"prog_ui", "attempt", "1", R.from, num(R.ft), num(run_time(now)), num(fail)});
        } else apply({"prog_ui", "attempt", "0", "", "-1", "-1", "-1"});
    }
    memcpy(R.last, me.pos, sizeof R.last);
    R.last_at = now;
    R.last_time = run_time(now);
    R.have = true;
}

// ---------------------------------------------------------------- console-style lines
std::vector<std::string> tokens(const char *line)
{
    std::vector<std::string> v;
    for (const char *p = line; *p;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        std::string t;
        if (*p == '"') {
            for (p++; *p && *p != '"'; p++) t += *p;
            if (*p) p++;
        } else
            while (*p && *p != ' ' && *p != '\t') t += *p++;
        v.push_back(t);
    }
    return v;
}

std::string level_map(const char *level)   // "maps/bhop_x.bsp" -> "bhop_x"
{
    std::string s = level ? level : "";
    size_t a = s.find_last_of("/\\");
    if (a != std::string::npos) s = s.substr(a + 1);
    if (s.size() > 4 && !strcasecmp(s.c_str() + s.size() - 4, ".bsp")) s.resize(s.size() - 4);
    return lower(s);
}

}   // namespace

// ---------------------------------------------------------------- the module's side
void pl_setup(IVEngineClient *engine, IBaseClientDLL *client, IClientEntityList *ents)
{
    g_eng = engine;
    g_cli = client;
    g_ents = ents;
    g_tables = (INetworkStringTableContainer *)plat_interface("engine", INTERFACENAME_NETWORKSTRINGTABLECLIENT);
    if (!S.loaded) load_settings();
}

void pl_level_init()
{
    if (!g_eng) return;
    const char *level = g_eng->GetLevelName();
    std::string map = level_map(level);
    if (map.empty()) return;
    load_map(map);
    load_zones(map);
    load_dests(level ? level : "");
    R.state = R_NONE;
    R.att = false;
    R.ended = false;
    R.have = false;
    R.trace.clear();
    R.best.clear();
    R.lost.clear();
    sync_book();
}

void pl_level_end()
{
    if (M.t_unsaved > 0) save_map();
    M.map.clear();
    g_srv_header.clear();
    std::lock_guard<std::mutex> l(g_zmu);
    g_zone_map.clear();
    g_zones.clear();
    g_zones_server = false;
}

void pl_frame()
{
    poll_server_zones();
    tick();
}

// prog_zones: this map's zones downloaded again now (the list may have changed; or to see why there are none)
void pl_refetch_zones()
{
    if (M.map.empty()) return;
    {
        std::lock_guard<std::mutex> l(g_zmu);
        if (g_zones_server) {
            say_line(std::string(PREFIX) + "This server sends its own timer zones, so the zone list isn't used here (prog_debug shows them).");
            return;
        }
        g_zone_status = "downloading again";
    }
    g_fetch_map = M.map;
    plat_http_init();
    plat_thread(fetch_thread);
    say_line(std::string(PREFIX) + "Downloading this map's timer zones again (prog_debug shows the result).");
}

// the book's `sm_progui ...` lines
void pl_command(const char *line)
{
    std::vector<std::string> v = tokens(line);
    if (v.size() < 2 || v[0] != "sm_progui") return;
    const std::string &verb = v[1];
    std::string a = v.size() > 2 ? v[2] : "", b = v.size() > 3 ? v[3] : "", c = v.size() > 4 ? v[4] : "";
    if (verb == "hello" || verb == "sync") sync_book();
    else if (verb == "view") {
        g_viewing = atoi(a.c_str()) != 0;
        if (g_viewing) send_cfg();
    } else if (verb == "add") {
        if (!add_points(a, b)) apply({"prog_ui", "msg", "2:24 or 224, Start, End or a spot"});
    } else if (verb == "edit") {
        Entry *e = find_entry(atoi(a.c_str()));
        if (!e || (b != "from" && b != "to")) return;
        std::string text;
        float t;
        if (!parse_point(c, text, t)) { apply({"prog_ui", "msg", "2:24 or 224, Start, End or a spot"}); return; }
        if (b == "from") { e->from = text; e->ft = t; }
        else { e->to = text; e->tt = t; }
        save_map();
        send_entry(*e);
    } else if (verb == "del") {
        int id = atoi(a.c_str());
        size_t n = M.e.size();
        M.e.erase(std::remove_if(M.e.begin(), M.e.end(), [id](const Entry &e) { return e.id == id; }), M.e.end());
        if (M.e.size() != n) save_map();
        if (R.att_entry == id) R.att_entry = 0;
        if (R.ended_entry == id) R.ended_entry = 0;
        apply({"prog_ui", "d", std::to_string(id)});
    } else if (verb == "spot") {   // spot "<name>" "<from>" ["<to>"]  (or "<from>-<to>" in one)
        float lo, hi = -1;
        bool ok = parse_range(b, lo, hi);
        if (ok && !c.empty() && c != b) ok = parse_clock(trim(c), hi);
        if (ok) set_spot(a, lo, hi);
        else apply({"prog_ui", "msg", "times like 3:40 and 3:55"});
    } else if (verb == "spotdel") delete_spot(a);
    else if (verb == "spotrename") rename_spot(a, b);
    else if (verb == "past") {   // the log's "past" filter: a run time or a spot's name, "" = off
        S.past = sanitize(a).substr(0, MAX_TEXT - 1);
        save_settings();
        send_cfg();
    }
    else if (verb == "set") {
        int n = atoi(b.c_str());
        if (a == "autofinish") S.autofinish = n != 0;
        else if (a == "saveat") S.saveat_here = n != 0;
        else if (a == "zero") S.zero_clock = n != 0;
        else if (a == "autosave") S.autosave = n != 0;
        else if (a == "filter") S.filter = n < 0 || n > 2 ? 0 : n;
        else if (a == "chatsound") S.chat_sound = n != 0;
        else if (a == "bin") S.bin = n < 5 ? 5 : n > 60 ? 60 : n;
        save_settings();
        send_cfg();
    }
}

// !prog [<from> to <to>], !progsave, !progmenu (the book), from chat (true = taken) or the sm_ console commands
bool pl_chat(const char *text0)
{
    std::string text = trim(text0 ? text0 : "");
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = trim(text.substr(1, text.size() - 2));
    if (text.size() < 2 || (text[0] != '!' && text[0] != '/')) return false;
    size_t e = text.find_first_of(" \t");
    std::string verb = lower(text.substr(1, e == std::string::npos ? std::string::npos : e - 1));
    std::string rest = e == std::string::npos ? "" : trim(text.substr(e));
    if (verb == "prog" || verb == "progmenu") {
        if (verb == "prog" && !rest.empty()) add_typed(rest);
        else gui_prog_open();
        return true;
    }
    if (verb == "progsave") { progsave(); return true; }
    return false;
}

void pl_console(const char *verb, const char *args)
{
    std::string line = std::string("!") + verb + " " + (args ? args : "");
    pl_chat(line.c_str());
}

// pl_debug: what the run tracking sees
void pl_debug()
{
    Me me = {};
    bool ok = read_me(me);
    double now = plat_now();
    char b[512];
    std::string zs;
    {
        std::lock_guard<std::mutex> l(g_zmu);
        zs = g_zone_status;
    }
    snprintf(b, sizeof b, "map %s | %s | %d teleport destinations | netvars life %d flags %d vel %d | me %s pos %.0f %.0f %.0f "
             "alive %d | state %d track %d time %.2f | attempt %d from \"%s\" %.2f | trace %d best %d",
             M.map.c_str(), zs.c_str(), (int)g_dests.size(), NV.life, NV.flags, NV.vel, ok ? "ok" : "-", me.pos[0], me.pos[1], me.pos[2],
             me.alive, R.state, R.track, run_time(now), R.att, R.from.c_str(), R.ft, (int)R.trace.size(), (int)R.best.size());
    say_line(b);
    if (FILE *f = fopen((dir_path() + SEP + "debug.txt").c_str(), "a")) { fprintf(f, "%s\n", b); fclose(f); }   // (the console log is often empty)
}
