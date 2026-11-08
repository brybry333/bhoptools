// Whole-file reads for the updaters (release and dev, both platforms).
#pragma once

#include <cstdio>
#include <string>

// The file's bytes, empty if it can't be opened.
inline std::string ReadAll(const std::string& path)
{
	std::string data;
	if (FILE* f = fopen(path.c_str(), "rb")) {
		char chunk[4096];
		size_t n;
		while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) data.append(chunk, n);
		fclose(f);
	}
	return data;
}
