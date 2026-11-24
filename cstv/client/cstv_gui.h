// The in-game CS:TV window (!cstv), drawn through the game's VGUI surface so it works on both
// renderers (togl and DXVK). Implemented in cstv_gui.cpp; the core (cstv_client.cpp) owns the
// helper connection and calls into the GUI. All functions run on the game's main thread.
#pragma once

// Core -> GUI
bool gui_install();                       // once, after the game's interfaces are up; hooks paint + input
void gui_toggle();                        // !cstv / `cstv` with no arguments
bool gui_open();
void gui_on_line(const char *line);       // every line from cstvd (srv ..., result ..., pref ..., master ..., stream_state ...)
bool gui_hud_on();                        // the HUD screen is drawn now (volume, uploads)
int gui_screen_alpha();                   // placed screens' opacity, 25-255 (Screen tab)
bool gui_preview_on();                    // the Lobby page shows the video (upload frames; full volume, like the HUD screen)
bool gui_xchat_on();                      // the Lobby page's Live tab shows the stream chat (upload its frames)
void gui_hud_toggle();                    // !tv hud / `cstv hud`
const char *gui_shared_audience();        // who sees the shared screen here, e.g. "lobby K7Q2MX (3 players here)"
bool gui_in_lobby();                      // the hub says we're in a lobby (for !leave)
bool gui_hub_up();                        // cstvd is connected to cstv.xyz
// The Progress Log (cstv_prog.inc): the same window with its own pages, fed by its backend on this PC
// (cstv_proglocal.cpp; servers' progresslog plugins are ignored)
void gui_prog_cmd(int argc, const char *const *argv, bool test);   // `prog_ui ...` typed or from a server: only test = `prog_ui_test`
void gui_prog_apply(int argc, const char *const *argv);           // the backend's `prog_ui ...` lines
void gui_prog_open();
void gui_prog_toggle();                     // `cstv_book`: opens or closes it
void gui_prog_level_end();

// The Progress Log's backend (cstv_proglocal.cpp): the log in files on this PC, runs followed in the game
class IVEngineClient;
class IBaseClientDLL;
class IClientEntityList;
void pl_setup(IVEngineClient *engine, IBaseClientDLL *client, IClientEntityList *ents);   // interfaces up
void pl_level_init();                       // LevelInitPostEntity
void pl_level_end();                        // LevelShutdown
void pl_frame();                            // once a frame
void pl_command(const char *line);          // the book's `sm_progui ...` lines
bool pl_chat(const char *text);             // a chat line: !prog, !progsave, !progmenu taken (true)
void pl_console(const char *verb, const char *args);   // sm_prog / sm_progsave / sm_progmenu typed or bound
void pl_debug();                            // `prog_debug`: what the run tracking sees
void pl_refetch_zones();                    // `prog_zones`: this map's zones downloaded again

// GUI -> core (implemented in cstv_client.cpp)
bool cstv_enabled();                      // bhoptools' Options switch (cstv_enable)
void cstv_helper_line(const char *line);  // send one line to cstvd (no trailing newline needed)
void cstv_print(const char *text);        // a CS:TV chat line for this player only
void cstv_chat_line(const char *line);    // a chat line as it is (colour codes and all) for this player only
void cstv_volume_preview(int vol);        // the volume slider while it's dragged (0-100; pref vol saves it)

// Place mode: the core traces and draws the preview; the GUI swallows its keys and draws the hint bar.
enum { PLACE_SIZE, PLACE_DIST, PLACE_FLOAT, PLACE_KIND, PLACE_CONFIRM, PLACE_CANCEL };
struct PlaceHint { bool personal, floating, want_float, ok; float scale, want_scale, dist; };
void cstv_place_begin(bool personal);     // closes the window; chat message instead when not on a server
void cstv_place_event(int ev, int n);     // n: wheel notches (SIZE), +1 further / -1 closer (DIST)
bool cstv_place_hint(PlaceHint *h);       // false = not placing; h may be null
bool cstv_personal_screen(float *scale, bool *floating);   // false = none on this map
void cstv_remove_personal();
