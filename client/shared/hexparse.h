// Hex as the server plugins send it. SourcePawn's %x writes a 32-bit cell, and a negative one
// with a minus sign; floats travel as their bits. Not strtoul: it saturates on Windows' 32-bit
// long, and glibc 2.38 headers redirect it to __isoc23_strtoul, which older runtimes lack.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

inline int HexDigit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// Stops at the first character that isn't a hex digit; wraps past 32 bits.
inline uint32_t ParseHexBits(const char* s)
{
	const bool neg = *s == '-';
	if (neg) ++s;
	uint32_t bits = 0;
	for (int d; (d = HexDigit(*s)) >= 0; ++s) bits = bits << 4 | (uint32_t)d;
	return neg ? 0u - bits : bits;
}

inline float HexBitsFloat(uint32_t bits)
{
	float f;
	memcpy(&f, &bits, 4);
	return f;
}

// A float from the server that the prediction uses as a setting: NaN, infinities and the absurd become 0.
inline float HexFloatSetting(const char* s)
{
	const float f = HexBitsFloat(ParseHexBits(s));
	return std::isfinite(f) && fabsf(f) < 1e6f ? f : 0.0f;
}
