// Main thread only.
#pragma once

struct MouseSplit {
	int    accumX = 0;
	int    accumY = 0;
	double sampleTime = 0.0;
	double splitTime  = -1.0; // where the last consumer stopped; -1 = never
};

inline void MouseSplitAdd(MouseSplit& m, int dx, int dy, double now)
{
	m.accumX += dx;
	m.accumY += dy;
	m.sampleTime = now;
}

inline void MouseSplitClear(MouseSplit& m)
{
	m.accumX = m.accumY = 0;
}

// Assumes the motion was linear between the last split and the newest event; split == 0 takes all.
// This form works on accumulators kept elsewhere (Windows: the engine's CInputSystem members).
// neverSplit is the caller's own test: MouseSplit starts at -1 and tests < 0; Windows keeps
// RawInput2's == 0, which also takes a split before the first sample (-0.01) as a real one.
inline void MouseSplitTake(int& accumX, int& accumY, double& splitTime, bool neverSplit, double sampleTime, int& outX, int& outY, double split)
{
	if (neverSplit) splitTime = sampleTime - 0.01;

	const double dt = splitTime - sampleTime;
	if ((dt < 0 ? -dt : dt) < 0.000001) {
		outX = outY = 0;
		return;
	}

	if (split == 0.0 || split >= sampleTime) {
		outX = accumX;
		outY = accumY;
		accumX = accumY = 0;
		splitTime = sampleTime;
		return;
	}

	if (split >= splitTime) {
		const float seg = (float)((split - splitTime) / (sampleTime - splitTime));
		outX = (int)(seg * (float)accumX);
		outY = (int)(seg * (float)accumY);
		accumX -= outX;
		accumY -= outY;
		splitTime = split;
		return;
	}

	outX = outY = 0;
}

inline void MouseSplitTake(MouseSplit& m, int& outX, int& outY, double split)
{
	MouseSplitTake(m.accumX, m.accumY, m.splitTime, m.splitTime < 0.0, m.sampleTime, outX, outY, split);
}
