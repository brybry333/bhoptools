// What the Windows and Linux hooks do around the engine's own calls. The platform files keep
// only the hook plumbing (Detours or vtable patches, SDL or the window procedure) and call these.
// Game thread only.
#pragma once

#include "checkpoint_predict.h"
#include "fastdl.h"
#include "options_tab.h"
#include "settings.h"
#include "spectate.h"
#include "teleport_predict.h"

namespace core {

// Before CHLClient::LevelInitPreEntity; returns the map name to pass on to it.
inline const char* OnLevelInit(const char* mapname, bool cpReady, bool tpReady)
{
	settings::ApplyRawInput(true);
	if (cpReady) cp::OnLevelInit();
	// A same-map reconnect keeps the level name, so the sim can't see it itself.
	if (tpReady) tp::OnLevelInit();
	return fastdl::ServerMapName(mapname);
}

// Trigger prediction follows the saved setting.
inline void SyncPredict()
{
	const bool on = settings::Get().predict;
	if (tp::Enabled() != on) tp::SetEnabled(on);
}

// F8. Returns the new state for the on-screen message.
inline bool TogglePredict()
{
	Settings& s = settings::Get();
	s.predict = !s.predict;
	settings::Changed();
	return s.predict;
}

// GameUI and the client may load after us, and a failed look is a few ms of scanning, so this
// tries the Options tab and spectate installs at most once a second until both are in.
inline void PollLateInstalls(double nowSeconds)
{
	static bool optionsDone = false, spectateDone = false;
	static double next = 0.0;
	if ((optionsDone && spectateDone) || nowSeconds < next) return;
	if (!optionsDone) optionsDone = options::Install();
	if (!spectateDone) spectateDone = spectate::Install();
	next = nowSeconds + 1.0;
}

}
