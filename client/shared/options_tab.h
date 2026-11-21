#pragma once

#include <cstdio>
#include <string>

namespace options {

// Game thread. False until GameUI is loaded; call again later.
bool Install();
#ifdef _WIN32
void Uninstall();
#endif

// bgs-jumpstats' showkeys for our own play once the player has moved them: `text` (UTF-8, lines split by
// \n) drawn over the game in the centre-print font's face and size at a HUD position, which centre text
// can't take. x and y are game_text coordinates (0-1 from the top left, -1 centres). Hidden a second after
// the last SetKeys. Game thread.
void SetKeys(const std::string& text, float x, float y);
void ClearKeys();
// The overlay's hook is in, so the client can say it draws its own showkeys (kClientJumpstats12)
bool KeysOverlay();

// GitHub's JSON for APP_REPO's latest release (any thread): when its tag is newer than APP_VERSION the tab says
// "Update available", with a link to the releases page. Only a notice: nothing is downloaded or installed.
void ReadLatestRelease(FILE* json);

}
