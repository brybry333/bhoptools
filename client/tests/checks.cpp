// Checks for the client code that reads untrusted input without the game: the update notice's release tag and the
// lump checksum list. Build and run: g++ -std=c++14 -Iclient/shared client/tests/checks.cpp -o checks && ./checks
#include <cassert>
#include <cstdio>
#include <cstring>
#include "lump_checksums.h"
#include "update_check.h"

int main()
{
	// versions: GitHub's tag (with a v) against this build's
	assert(NewerVersion("v1.14.2-fork.1", "1.14.1-fork.1"));
	assert(NewerVersion("v1.14.1-fork.2", "1.14.1-fork.1"));
	assert(NewerVersion("v1.14.10", "1.14.9-fork.1"));
	assert(NewerVersion("v1.15.0", "1.14.1-fork.1"));
	assert(!NewerVersion("v1.14.1-fork.1", "1.14.1-fork.1"));   // the same release: no notice
	assert(!NewerVersion("v1.14.1", "1.14.1-fork.1"));
	assert(!NewerVersion("v1.9.9", "1.14.1-fork.1"));
	assert(!NewerVersion("", "1.14.1-fork.1"));

	// the tag in GitHub's JSON (the same text in the release notes doesn't count; odd or long tags don't either)
	char tag[32];
	assert(ParseReleaseTag("{\"url\":\"x\",\"tag_name\": \"v1.14.2-fork.1\",\"body\":\"tag_name\"}", tag) && !strcmp(tag, "v1.14.2-fork.1"));
	assert(ParseReleaseTag("{\n  \"tag_name\":\"v2\"\n}", tag) && !strcmp(tag, "v2"));
	assert(!ParseReleaseTag("{\"message\":\"API rate limit exceeded\"}", tag));
	assert(!ParseReleaseTag("{\"tag_name\":\"v9<script>\"}", tag));
	assert(!ParseReleaseTag("{\"tag_name\":\"v9.0.0-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}", tag));
	assert(!ParseReleaseTag("{\"tag_name\":\"v9.0.0", tag));
	assert(!ParseReleaseTag("", tag));

	// lump_checksums.csv (downloaded): "<sha1>,<md5>"; the sha1 becomes a file name, so only hex
	unsigned char md5[16];
	char key[33], sha1[41], csv[256];
	for (int i = 0; i < 16; ++i) md5[i] = (unsigned char)(i * 17);
	for (int i = 0; i < 16; ++i) snprintf(key + i * 2, 3, "%02x", md5[i]);
	snprintf(csv, sizeof csv, "aaaa,bbbb\n0123456789abcdef0123456789abcdef01234567,%s\n", key);
	assert(LumpChecksumsFindSha1(csv, md5, sha1) && !strcmp(sha1, "0123456789abcdef0123456789abcdef01234567"));
	snprintf(csv, sizeof csv, "0123456789abcdef0123456789abcdef01234567,%s\r\n", key);
	assert(LumpChecksumsFindSha1(csv, md5, sha1));
	snprintf(csv, sizeof csv, "../../../../../../../../../../../../../x,%s\n", key);
	assert(!LumpChecksumsFindSha1(csv, md5, sha1));

	puts("checks: ok");
	return 0;
}
