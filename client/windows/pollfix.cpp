// The engine reads input through an unfiltered PeekMessage(NULL, 0, 0, PM_REMOVE) loop once a frame.
// An 8 kHz mouse puts a WM_INPUT and a cursor move in that queue every 125 us. Each retrieval is a
// kernel transition, and each cursor move also has win32k send WM_NCHITTEST and WM_SETCURSOR to the
// window proc on the way out, so the loop retrieves tens of thousands of messages a second and runs
// long past where it would otherwise be empty. Handling them cheaply doesn't help; not retrieving
// them does. So, while the fix is on, the game is in front and you're in game (no cursor): the raw
// queue is read in one GetRawInputBuffer batch at the start of each pump and WM_INPUT is never
// retrieved, the coalesced WM_MOUSEMOVE and WM_NCMOUSEMOVE are taken at most once per pump (they
// carry the latest position), and everything else comes through range-filtered peeks as before.
// Menus get the plain pump: filtered there, a click in a full-screen menu (paint, triggerlook) could
// show the borderless window black for a frame on Windows (multithreaded rendering, first start).
#include "pollfix.h"
#include "utils.h"
#include "settings.h"
#include "game_layout.h"
#include "sigs.h"
#include "Detours/src/detours.h"

#include <intrin.h>
#include <cstdlib>
#include <cstring>

namespace pollfix {
namespace {

typedef BOOL(WINAPI* PeekMessageFn)(LPMSG, HWND, UINT, UINT, UINT);
PeekMessageFn oPeekMessageA;
PeekMessageFn oPeekMessageW;

int (*g_drain)();
bool (*g_rawActive)();
LONGLONG g_freq;
DWORD g_gameThread;
HWND g_gameWindow;
UINT g_shutdownMsg;  // sent by Shutdown, handled in OnWindowMessage
MemRange g_callers[2];  // inputsystem.dll and engine.dll: only their pumps are filtered

// A pump runs from the first call after one returned FALSE to the next FALSE.
bool g_inPump;
bool g_filtering;
LONGLONG g_pumpStart;
bool g_tookMouseMove, g_tookNcMouseMove, g_tookUnfiltered;
bool g_updatedSinceFrame;  // a pump ran UpdateNoLegacy since the last OnFrame

// No-legacy mode: the mouse registered with RIDEV_NOLEGACY while looking around, so Windows stops
// making a WM_MOUSEMOVE (with its WM_NCHITTEST and WM_SETCURSOR) for every report. Filtering the
// pump still leaves win32k doing that work per report; this is the only way to stop it. Part of
// the high polling rate fix.
bool g_noLegacyWanted = true;
bool g_noLegacyOn;
bool g_registerFailed;
WPARAM g_buttonsDown;  // MK_* we've posted down and not up

LONGLONG Now()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

bool FindMouseRegistration(RAWINPUTDEVICE& out)
{
	RAWINPUTDEVICE devs[16];
	UINT n = 16;
	n = GetRegisteredRawInputDevices(devs, &n, sizeof(RAWINPUTDEVICE));
	if (n == (UINT)-1) return false;
	for (UINT i = 0; i < n; ++i) {
		if (devs[i].usUsagePage == 1 && devs[i].usUsage == 2) {
			out = devs[i];
			return true;
		}
	}
	return false;
}

void PostButton(UINT msg, WPARAM extra = 0)
{
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(g_gameWindow, &p);
	PostMessageW(g_gameWindow, msg, g_buttonsDown | extra, MAKELPARAM(p.x, p.y));
}

void ReleaseButtons()
{
	const struct { WPARAM mk; UINT up; WPARAM x; } buttons[] = {
		{ MK_LBUTTON, WM_LBUTTONUP, 0 }, { MK_RBUTTON, WM_RBUTTONUP, 0 }, { MK_MBUTTON, WM_MBUTTONUP, 0 },
		{ MK_XBUTTON1, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1) }, { MK_XBUTTON2, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2) },
	};
	for (const auto& b : buttons) {
		if (!(g_buttonsDown & b.mk)) continue;
		g_buttonsDown &= ~b.mk;
		PostButton(b.up, b.x);
	}
}

// What CInput::MouseMove checks. The Windows cursor misses chat, which only shows one after a cursor message.
bool InGame()
{
	static void* surface;  // looked up until it's there: the pump runs from before vguimatsurface loads
	if (!surface) surface = CreateInterfaceIn(SURFACE_MODULE, "VGUI_Surface030");
	if (surface) return !VFunc<bool (*)(void*)>(surface, VT_Surface_IsCursorVisible)(surface);
	CURSORINFO ci = { sizeof(ci) };
	return GetCursorInfo(&ci) && (!(ci.flags & CURSOR_SHOWING) || !ci.hCursor);
}

// Once per pump, or per frame when no pump ran: no-legacy only while the game is in front and
// looking around (menus need the cursor messages) and our raw path sees every report (it posts the
// buttons). pumpSeen false (no recognised pump lately) turns it off.
void UpdateNoLegacy(bool pumpSeen)
{
	const bool want = settings::Get().pollfix && g_noLegacyWanted && g_gameWindow && pumpSeen
		&& GetForegroundWindow() == g_gameWindow && g_rawActive && g_rawActive() && InGame();
	if (want == g_noLegacyOn) return;
	RAWINPUTDEVICE d;
	if (!FindMouseRegistration(d)) return;
	d.dwFlags = want ? (d.dwFlags | RIDEV_NOLEGACY) : (d.dwFlags & ~RIDEV_NOLEGACY);
	if (!RegisterRawInputDevices(&d, 1, sizeof(d))) {
		// Turning off is retried every pump, so log it once until it works.
		if (!g_registerFailed)
			CoreLog("no-legacy: RegisterRawInputDevices failed (%lu), turning it off", GetLastError());
		g_registerFailed = true;
		g_noLegacyWanted = false;
		return;
	}
	g_registerFailed = false;
	g_noLegacyOn = want;
	if (!want) ReleaseButtons();
}

const char* CallerName(const void* ret)
{
	const uintptr_t r = reinterpret_cast<uintptr_t>(ret);
	if (r >= g_callers[0].start && r < g_callers[0].end) return "inputsystem";
	if (r >= g_callers[1].start && r < g_callers[1].end) return "engine";
	return nullptr;
}

BOOL Pump(PeekMessageFn fn, LPMSG msg, HWND hwnd, UINT flags)
{
	if (!g_inPump) {
		g_inPump = true;
		g_pumpStart = Now();
		g_tookMouseMove = g_tookNcMouseMove = g_tookUnfiltered = false;
		g_filtering = g_drain && settings::Get().pollfix && GetForegroundWindow() == g_gameWindow && InGame();
		if (g_filtering)
			g_drain();
		// After the drain: what's queued arrived under the old registration and is handled by it.
		UpdateNoLegacy(true);
		g_updatedSinceFrame = true;
	}
	BOOL r;
	if (!g_filtering) {
		r = fn(msg, hwnd, 0, 0, flags);
	} else {
		// Everything but WM_NCMOUSEMOVE (0xA0), WM_INPUT (0xFF) and WM_MOUSEMOVE (0x200).
		r = fn(msg, hwnd, 0, WM_NCMOUSEMOVE - 1, flags)
			|| fn(msg, hwnd, WM_NCMOUSEMOVE + 1, WM_INPUT - 1, flags)
			|| fn(msg, hwnd, WM_INPUT + 1, WM_MOUSEMOVE - 1, flags)
			|| fn(msg, hwnd, WM_MOUSEMOVE + 1, 0xFFFFFFFF, flags);
		if (!r && !g_tookMouseMove) {
			g_tookMouseMove = true;
			r = fn(msg, hwnd, WM_MOUSEMOVE, WM_MOUSEMOVE, flags);
		}
		if (!r && !g_tookNcMouseMove) {
			g_tookNcMouseMove = true;
			r = fn(msg, hwnd, WM_NCMOUSEMOVE, WM_NCMOUSEMOVE, flags);
		}
		// Raw input the batch read didn't take (WM_INPUT_DEVICE_CHANGE arrives through the raw queue,
		// and a ranged peek can't retrieve it): one plain peek per pump, so it reaches the engine.
		if (!r && !g_tookUnfiltered && (HIWORD(GetQueueStatus(QS_RAWINPUT)) & QS_RAWINPUT)) {
			g_tookUnfiltered = true;
			r = fn(msg, hwnd, 0, 0, flags);
		}
	}
	if (!r)
		g_inPump = false;
	return r;
}

BOOL Hook(PeekMessageFn fn, LPMSG msg, HWND hwnd, UINT min, UINT max, UINT flags, const void* ret)
{
	if (GetCurrentThreadId() != g_gameThread)
		return fn(msg, hwnd, min, max, flags);
	const bool pump = CallerName(ret) && min == 0 && max == 0 && (flags & ~PM_NOYIELD) == PM_REMOVE
		&& (hwnd == NULL || hwnd == g_gameWindow);
	if (!pump)
		return fn(msg, hwnd, min, max, flags);
	return Pump(fn, msg, hwnd, flags);
}

BOOL WINAPI Hooked_PeekMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT flags)
{
	return Hook(oPeekMessageA, msg, hwnd, min, max, flags, _ReturnAddress());
}

BOOL WINAPI Hooked_PeekMessageW(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT flags)
{
	return Hook(oPeekMessageW, msg, hwnd, min, max, flags, _ReturnAddress());
}

void Cmd_PollFix(const CCommandX& c)
{
	if (c.argc > 1) {
		settings::Get().pollfix = atoi(c.argv[1]) != 0;
		settings::Changed();
	}
	CoreLog("bhoptools_pollfix %d%s", settings::Get().pollfix ? 1 : 0, g_drain ? "" : " (unavailable, see the missing signature above)");
}

ConCommandX g_cmds[1];

}  // namespace

void Init(int (*drain)(), bool (*rawActive)())
{
	g_drain = drain;
	g_rawActive = rawActive;
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart;
	g_shutdownMsg = RegisterWindowMessageW(L"bhoptools_pollfix_shutdown");
	const char* modules[2] = { "inputsystem.dll", "engine.dll" };
	for (int i = 0; i < 2; ++i) {
		MODULEINFO mi{};
		HMODULE h = GetModuleHandleA(modules[i]);
		if (h && GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi)))
			g_callers[i] = { (uintptr_t)mi.lpBaseOfDll, (uintptr_t)mi.lpBaseOfDll + mi.SizeOfImage };
	}
	HMODULE user32 = GetModuleHandleA("user32.dll");
	oPeekMessageA = user32 ? (PeekMessageFn)GetProcAddress(user32, "PeekMessageA") : nullptr;
	oPeekMessageW = user32 ? (PeekMessageFn)GetProcAddress(user32, "PeekMessageW") : nullptr;
	if (!oPeekMessageA || !oPeekMessageW) {
		CoreLog("high polling rate fix: PeekMessage not found, unavailable");
		g_drain = nullptr;
	} else if (!g_drain) {
		CoreLog("high polling rate fix: unavailable (the raw input accumulators weren't found)");
	} else {
		CoreLog("high polling rate fix: %s (Options > bhoptools, or bhoptools_pollfix 0/1)", settings::Get().pollfix ? "on" : "off");
	}
}

void Attach()
{
	if (oPeekMessageA) DetourAttach(&(PVOID&)oPeekMessageA, Hooked_PeekMessageA);
	if (oPeekMessageW) DetourAttach(&(PVOID&)oPeekMessageW, Hooked_PeekMessageW);
}

void Detach()
{
	if (oPeekMessageA) DetourDetach(&(PVOID&)oPeekMessageA, Hooked_PeekMessageA);
	if (oPeekMessageW) DetourDetach(&(PVOID&)oPeekMessageW, Hooked_PeekMessageW);
}

void Shutdown()
{
	// The registration, the window and the posted buttons all belong to the game thread, so it
	// does the work. The timeout covers a map load, which doesn't pump.
	if (g_gameWindow && g_shutdownMsg)
		SendMessageTimeoutW(g_gameWindow, g_shutdownMsg, 0, 0, SMTO_NORMAL, 10000, nullptr);
}

bool OnWindowMessage(HWND hwnd, UINT msg)
{
	if (!g_gameWindow) {
		g_gameWindow = hwnd;
		g_gameThread = GetCurrentThreadId();
	}
	if (g_shutdownMsg && msg == g_shutdownMsg) {
		g_noLegacyWanted = false;
		UpdateNoLegacy(false);
		return true;
	}
	return false;
}

// Another PeekMessage hook installed after ours (an overlay) hides the pump's caller, and then
// nothing would re-evaluate no-legacy: left on in a menu, there's no cursor.
void OnFrame()
{
	if (GetCurrentThreadId() != g_gameThread) return;
	if (g_updatedSinceFrame) {
		g_updatedSinceFrame = false;
		return;
	}
	UpdateNoLegacy(g_pumpStart && (Now() - g_pumpStart) * 10 < g_freq);
}

void OnRawMouse(const RAWMOUSE& m)
{
	if (!g_noLegacyOn || !m.usButtonFlags) return;
	USHORT f = m.usButtonFlags;
	// Raw input reports the physical buttons; Windows' "swap primary and secondary" only changes the messages.
	if (GetSystemMetrics(SM_SWAPBUTTON)) {
		const USHORT left = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP, right = RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP;
		f = (USHORT)((f & ~(left | right)) | (f & left) << 2 | (f & right) >> 2);
	}
	const struct { USHORT down, up; WPARAM mk; UINT msgDown, msgUp; WPARAM x; } buttons[] = {
		{ RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, MK_LBUTTON, WM_LBUTTONDOWN, WM_LBUTTONUP, 0 },
		{ RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, MK_RBUTTON, WM_RBUTTONDOWN, WM_RBUTTONUP, 0 },
		{ RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, MK_MBUTTON, WM_MBUTTONDOWN, WM_MBUTTONUP, 0 },
		{ RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, MK_XBUTTON1, WM_XBUTTONDOWN, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1) },
		{ RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, MK_XBUTTON2, WM_XBUTTONDOWN, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2) },
	};
	for (const auto& b : buttons) {
		if (f & b.down) { g_buttonsDown |= b.mk; PostButton(b.msgDown, b.x); }
		if (f & b.up) { g_buttonsDown &= ~b.mk; PostButton(b.msgUp, b.x); }
	}
	// The engine reads an empty wheel report as wheel down.
	if ((f & RI_MOUSE_WHEEL) && (SHORT)m.usButtonData != 0) {
		// Wheel messages carry screen coordinates, unlike the buttons.
		POINT p;
		GetCursorPos(&p);
		PostMessageW(g_gameWindow, WM_MOUSEWHEEL, MAKEWPARAM(g_buttonsDown, (SHORT)m.usButtonData), MAKELPARAM(p.x, p.y));
	}
}

void RegisterCommands()
{
	static bool done;
	if (done) return;
	done = true;
	void* cvar = CreateInterfaceIn(VSTDLIB_MODULE, "VEngineCvar004");
	void* echo = cvar ? VFunc<void* (*)(void*, const char*)>(cvar, VT_Cvar_FindBase)(cvar, "echo") : nullptr;
	const char* echoName = echo ? static_cast<ConCommandX*>(echo)->name : nullptr;
	if (!echo || !VFunc<bool (*)(void*)>(echo, VT_CCB_IsCommand)(echo) || !echoName || strcmp(echoName, "echo")) {
		CoreLog("pollfix: couldn't register bhoptools_pollfix");
		return;
	}
	const struct { const char* name; const char* help; void (*fn)(const CCommandX&); } defs[] = {
		{ "bhoptools_pollfix", "High polling rate fix: in game, no legacy mouse messages and a filtered message pump; menus are left alone (0/1, on by default).", Cmd_PollFix },
	};
	for (int i = 0; i < (int)(sizeof(defs) / sizeof(defs[0])); ++i) {
		ConCommandX& c = g_cmds[i];
		c.vtable = *reinterpret_cast<void**>(echo);
		c.name = defs[i].name;
		c.help = defs[i].help;
		c.callback = reinterpret_cast<void*>(defs[i].fn);
		c.bits = 0x02;  // new-style callback taking const CCommand&
		VFunc<void (*)(void*, void*)>(cvar, VT_Cvar_Register)(cvar, &c);
	}
}

}
