// Code signatures ("48 8B ? ? E8", ? or ?? for a wildcard byte), parsed once and scanned with
// memchr on the first fixed byte. Both platforms' FindPatternIn scan with this.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// Hand-rolled instead of sscanf/strtoul: glibc 2.38 headers redirect those to
// __isoc23_* symbols that older runtimes don't have.
inline const char* ParseHex(const char* s, uintptr_t* out)
{
	uintptr_t v = 0;
	for (;; ++s) {
		const char c = *s;
		int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else break;
		v = (v << 4) | (uintptr_t)d;
	}
	*out = v;
	return s;
}

struct ParsedSig {
	std::vector<uint8_t> bytes;
	std::vector<uint8_t> mask; // 1 = compare, 0 = wildcard
};

inline ParsedSig ParseSig(const char* sig)
{
	ParsedSig p;
	const char* s = sig;
	while (*s) {
		while (*s == ' ' || *s == '\t') ++s;
		if (!*s) break;
		if (*s == '?') {
			p.bytes.push_back(0);
			p.mask.push_back(0);
			++s;
			if (*s == '?') ++s;
		} else {
			char buf[3] = { s[0], s[1], 0 };
			uintptr_t v;
			ParseHex(buf, &v);
			p.bytes.push_back((uint8_t)v);
			p.mask.push_back(1);
			s += s[1] ? 2 : 1;
		}
	}
	return p;
}

inline uintptr_t ScanRange(uintptr_t start, uintptr_t end, const ParsedSig& p)
{
	const size_t n = p.bytes.size();
	if (!n || end <= start || (end - start) < n) return 0;

	size_t k = 0;
	while (k < n && !p.mask[k]) ++k;
	if (k == n) return start;

	const uint8_t* base = (const uint8_t*)start;
	const uint8_t* last = base + (end - start) - n;
	const uint8_t* cur = base;
	while (cur <= last) {
		const uint8_t* hit = (const uint8_t*)memchr(cur + k, p.bytes[k], (size_t)(last - cur) + 1);
		if (!hit) return 0;
		cur = hit - k;
		size_t j = 0;
		for (; j < n; ++j) {
			if (p.mask[j] && cur[j] != p.bytes[j]) break;
		}
		if (j == n) return (uintptr_t)cur;
		++cur;
	}
	return 0;
}
