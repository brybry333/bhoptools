#pragma once
// The Options tab's "update available" check, the parts that need no game (client/tests/checks.cpp tests them).
#include <cctype>
#include <cstdlib>
#include <cstring>

// a/b version tags ("v1.14.2-fork.1"): true when a's numbers, in order, are higher (a missing one counts lower)
inline bool NewerVersion(const char* a, const char* b)
{
	for (;;) {
		while (*a && !isdigit((unsigned char)*a)) ++a;
		while (*b && !isdigit((unsigned char)*b)) ++b;
		if (!*a || !*b) return *a != 0;
		char* end;
		const unsigned long na = strtoul(a, &end, 10);
		a = end;
		const unsigned long nb = strtoul(b, &end, 10);
		b = end;
		if (na != nb) return na > nb;
	}
}

// "tag_name" from GitHub's JSON for a release (untrusted): only a short plain tag, else false
inline bool ParseReleaseTag(const char* json, char tag[32])
{
	const char* p = strstr(json, "\"tag_name\"");
	if (!p) return false;
	p += 10;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') ++p;
	if (*p++ != '"') return false;
	size_t len = 0;
	while (len < 31 && (isalnum((unsigned char)p[len]) || p[len] == '.' || p[len] == '-' || p[len] == '_')) {
		tag[len] = p[len];
		++len;
	}
	tag[len] = 0;
	return len && p[len] == '"';
}
