#include "settings.h"
#include "utils.h"
#include "sigs.h"
#include "game_layout.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace settings {
namespace {

Settings g_settings;
std::function<void()> g_apply;
bool g_rawinputSaved;
bool g_lastRawinput = true;

struct Field {
	const char* key;
	bool Settings::* value;
};
const Field kFields[] = {
	{ "rawinput",  &Settings::rawinput },
	{ "predict",   &Settings::predict },
	{ "pingfix",   &Settings::predict },  // read from files saved before the rename
	{ "viewpunch", &Settings::viewpunch },
	{ "fastdl",    &Settings::fastdl },
	{ "speedo",    &Settings::speedo },
	{ "hudkeys",   &Settings::hudkeys },
	{ "jsframe",   &Settings::jsframe },
	{ "chatglyphs", &Settings::chatglyphs },
	{ "pollfix",   &Settings::pollfix },
	{ "rawprecise", &Settings::rawprecise },
	{ "curtimefix", &Settings::curtimefix },
	{ "cstv",      &Settings::cstv },
	{ "paint",     &Settings::paint },
	{ "f8bound",   &Settings::f8bound },
};

}

#define SETTINGS_DIR_NAME "bhoptools"

std::string Dir()
{
#ifdef _WIN32
	const char* base = getenv("APPDATA");
	return base ? std::string(base) + "\\" SETTINGS_DIR_NAME : std::string();
#else
	const char* xdg = getenv("XDG_CONFIG_HOME");
	const char* home = getenv("HOME");
	if (xdg && *xdg) return std::string(xdg) + "/" SETTINGS_DIR_NAME;
	return home ? std::string(home) + "/.config/" SETTINGS_DIR_NAME : std::string();
#endif
}


namespace {

void MakeDir(const std::string& dir)
{
#ifdef _WIN32
	_mkdir(dir.c_str());
#else
	mkdir(dir.c_str(), 0755);
#endif
}



std::string Path()
{
	const std::string dir = Dir();
	return dir.empty() ? dir : dir + "/settings.cfg";
}

void Save()
{
	const std::string dir = Dir();
	if (dir.empty()) return;
	MakeDir(dir);
	// Written aside and renamed over, so a crash mid-write keeps the old file.
	const std::string path = Path(), tmp = path + ".tmp";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	for (const Field& fl : kFields)
		if (strcmp(fl.key, "pingfix") && (fl.value != &Settings::rawinput || g_rawinputSaved))
			fprintf(f, "%s=%d\n", fl.key, g_settings.*fl.value ? 1 : 0);
	const bool ok = fclose(f) == 0;
#ifdef _WIN32
	if (ok) remove(path.c_str());
#endif
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
		remove(tmp.c_str());
		CoreLog("settings: couldn't write %s", path.c_str());
	}
}

}

Settings& Get() { return g_settings; }


void Load()
{
	const std::string path = Path();
	FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r");
	if (f) {
		char line[128];
		while (fgets(line, sizeof(line), f)) {
			char* eq = strchr(line, '=');
			if (!eq) continue;
			*eq = 0;
			for (const Field& fl : kFields)
				if (!strcmp(line, fl.key)) {
					g_settings.*fl.value = atoi(eq + 1) != 0;
					if (fl.value == &Settings::rawinput) g_rawinputSaved = true;
				}
		}
		fclose(f);
	}
	// Not in the public Options tab, so a value saved by an older build couldn't be changed back: the
	// polling rate fix is always on and the trainers are redrawn every tick
	g_settings.pollfix = true;
	g_settings.jsframe = false;
	g_lastRawinput = g_settings.rawinput;
}

void Changed()
{
	if (g_settings.rawinput != g_lastRawinput) g_rawinputSaved = true;
	g_lastRawinput = g_settings.rawinput;
	if (g_apply) g_apply();
	ApplyRawInput(false);
	Save();
}

void ApplyRawInput(bool levelLoad)
{
	void* cvar = CreateInterfaceIn(VSTDLIB_MODULE, "VEngineCvar004");
	void* engine = CreateInterfaceIn(ENGINE_MODULE, "VEngineClient014");
	char* var = cvar ? VFunc<char* (*)(void*, const char*)>(cvar, VT_Cvar_FindBase)(cvar, "m_rawinput") : nullptr;
	if (!var || !engine || strcmp(*reinterpret_cast<const char* const*>(var + OFF_ConVar_Name), "m_rawinput")) return;
	const int value = *reinterpret_cast<const int*>(ConVarValueOwner(var) + OFF_ConVar_IntValue);
	if (!g_rawinputSaved) {
		if (!levelLoad) return;
		g_rawinputSaved = true;
		g_settings.rawinput = g_lastRawinput = value != 0;
		Save();
	}
	if ((value != 0) == g_settings.rawinput) return;
	void* fn = (*reinterpret_cast<void***>(engine))[VT_IVEngineClient_ClientCmdUnrestricted];
	if (!InModuleCode(ENGINE_MODULE, fn)) return;
	reinterpret_cast<void (*)(void*, const char*)>(fn)(engine, g_settings.rawinput ? "m_rawinput 1\n" : "m_rawinput 0\n");
}

void DefaultBinds()
{
	if (g_settings.f8bound) return;
	void* engine = CreateInterfaceIn(ENGINE_MODULE, "VEngineClient014");
	void** vt = engine ? *reinterpret_cast<void***>(engine) : nullptr;
	if (!vt || !InModuleCode(ENGINE_MODULE, vt[VT_IVEngineClient_KeyLookupBinding])
			|| !InModuleCode(ENGINE_MODULE, vt[VT_IVEngineClient_KeyBindingForKey])
			|| !InModuleCode(ENGINE_MODULE, vt[VT_IVEngineClient_ClientCmdUnrestricted])) {
		CoreLog("keys: the engine's key functions aren't where expected, F8 left alone");
		return;
	}
	g_settings.f8bound = true;
	Save();
	const int kKeyF8 = 99;  // ButtonCode_t KEY_F8
	const char* onF8 = reinterpret_cast<const char* (*)(void*, int)>(vt[VT_IVEngineClient_KeyBindingForKey])(engine, kKeyF8);
	const char* key = reinterpret_cast<const char* (*)(void*, const char*)>(vt[VT_IVEngineClient_KeyLookupBinding])(engine, "bhoptools_predict_toggle");
	if (onF8 && *onF8) { CoreLog("keys: F8 is \"%s\", so the prediction toggle gets no default key", onF8); return; }
	if (key && *key) { CoreLog("keys: the prediction toggle is on %s already", key); return; }
	reinterpret_cast<void (*)(void*, const char*)>(vt[VT_IVEngineClient_ClientCmdUnrestricted])(engine, "bind F8 bhoptools_predict_toggle\n");
	CoreLog("keys: F8 -> bhoptools_predict_toggle (once; change it in Options > Keyboard)");
}

void SetApply(std::function<void()> fn) { g_apply = fn; }

}
