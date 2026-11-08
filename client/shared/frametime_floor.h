// The engine's 1 ms frame time floor (Host_AccumulateTime on Windows, Host_FilterTime on Linux):
// above 1000 fps it runs the client's clock ahead of real time. Both builds NOP the maxsd against
// MIN_FRAMETIME at `site`; the platform code makes the page writable.
#pragma once

#include <cstdint>
#include <cstring>

// An 8-byte rip-relative maxsd (F2 0F 5F xx disp32): the constant is at site + 8 + disp32.
// Only the stock 0.001 floor is patched; anything else is someone else's change.
inline bool FrametimeFloorPatchable(const uint8_t* site)
{
	double floor;
	int32_t disp;
	memcpy(&disp, site + 4, sizeof(disp));
	memcpy(&floor, site + 8 + disp, sizeof(floor));
	return floor == 0.001;
}

static const uint8_t kFrametimeFloorNop[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
