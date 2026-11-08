// The local player's fields both ProcessMovement hooks read (teleport_predict's trigger sim and
// checkpoint_predict), found once by name in the client's datamaps. Each field resolves on its
// own, so one table gives every caller the same offsets it would get from a table of its own.
#pragma once

#include "datamap.h"

namespace pf {

enum Field {
	BaseVelocity, Velocity, AbsVelocity, Flags, Gravity, TickBase, LifeState, MoveType,
	Local, Ducked, Ducking, Ducktime, StepSize,
	WaterLevel, GroundEntity, WaterJumpTime, SurfaceFriction, Stamina, ViewOffset,
	kCount
};

// Raw datamap offsets, -1 when not found and -2 when the datamaps disagree. Ducked, Ducking,
// Ducktime and StepSize are CPlayerLocalData members: relative to Local, not the player.
struct Offsets {
	int at[kCount];
	int operator[](Field f) const { return at[f]; }
};

inline const char* Name(Field f)
{
	static const char* const kNames[kCount] = {
		"m_vecBaseVelocity", "m_vecVelocity", "m_vecAbsVelocity", "m_fFlags", "m_flGravity", "m_nTickBase", "m_lifeState", "m_MoveType",
		"m_Local", "m_bDucked", "m_bDucking", "m_flDucktime", "m_flStepSize",
		"m_nWaterLevel", "m_hGroundEntity", "m_flWaterJumpTime", "m_surfaceFriction", "m_flStamina", "m_vecViewOffset",
	};
	return kNames[f];
}

inline Offsets Resolve(const std::vector<MemRange>& segs)
{
	using namespace dm;
	FieldReq req[kCount] = {
		{ Name(BaseVelocity),    FIELD_VECTOR,    12, {}, 0, -1 },
		{ Name(Velocity),        FIELD_VECTOR,    12, {}, 0, -1 },
		{ Name(AbsVelocity),     FIELD_VECTOR,    12, {}, 0, -1 },
		{ Name(Flags),           FIELD_INTEGER,   4,  {}, 0, -1 },
		{ Name(Gravity),         FIELD_FLOAT,     4,  {}, 0, -1 },
		{ Name(TickBase),        FIELD_INTEGER,   4,  {}, 0, -1 },
		{ Name(LifeState),       FIELD_CHARACTER, 1,  {}, 0, -1 },
		{ Name(MoveType),        FIELD_CHARACTER, 1,  {}, 0, -1 },
		{ Name(Local),           FIELD_EMBEDDED,  -1, {}, 0, -1 },
		{ Name(Ducked),          FIELD_BOOLEAN,   1,  {}, 0, -1 },
		{ Name(Ducking),         FIELD_BOOLEAN,   1,  {}, 0, -1 },
		{ Name(Ducktime),        FIELD_FLOAT,     4,  {}, 0, -1 },
		{ Name(StepSize),        FIELD_FLOAT,     4,  {}, 0, -1 },
		{ Name(WaterLevel),      FIELD_CHARACTER, 1,  {}, 0, -1 },
		{ Name(GroundEntity),    FIELD_EHANDLE,   4,  {}, 0, -1 },
		{ Name(WaterJumpTime),   FIELD_FLOAT,     4,  {}, 0, -1 },
		{ Name(SurfaceFriction), FIELD_FLOAT,     4,  {}, 0, -1 },
		{ Name(Stamina),         FIELD_FLOAT,     4,  {}, 0, -1 },
		{ Name(ViewOffset),      FIELD_VECTOR,    12, {}, 0, -1 },
	};
	Offsets o;
	for (int i = 0; i < kCount; ++i) o.at[i] = -1;
	if (segs.empty()) return o;
	ResolveFields(segs, req, kCount);
	for (int i = 0; i < kCount; ++i) o.at[i] = req[i].offset;
	return o;
}

}
