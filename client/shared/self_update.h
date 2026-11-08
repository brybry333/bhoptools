// The release updater both platforms run on start: find the newest release past ours, and check
// its signed SHA256SUMS before anyone is asked. The platform supplies the download and does the
// asking and installing (a MessageBox and the exe swap on Windows, SDL and the .so swap on Linux).
#pragma once

#include "release_sig.h"
#include "update_json.h"
#include "version.h"

#include <functional>
#include <string>

namespace upd {

// Downloads https://host + path into `body`; false on failure.
typedef std::function<bool(const char* host, const std::string& path, std::string& body)> Fetch;

struct Release {
	std::string tag;
	std::string changelog;  // every release since ours
	bool        required = false;  // installed even for those who chose never to update
	std::string sums;       // its SHA256SUMS, signature checked
	std::string expected;   // the asset's sha256 in it
	std::string url;        // https://github.com/.../download/<tag>/
};

enum class Found { Nothing, Unverified, Offer };

// Nothing: no newer release, or the player said never and it isn't required. Unverified: `why`
// says what failed, and nobody should be offered it. Offer: `r` is filled.
inline Found FindRelease(const Fetch& fetch, const char* asset, bool ignored, Release& r, std::string& why)
{
	std::string json;
	if (!fetch("api.github.com", "/repos/ddyfad/bhoptools/releases?per_page=20", json)) return Found::Nothing;
	r.tag = Changelog(json, APP_VERSION, r.changelog, &r.required);
	if (r.tag.empty() || (ignored && !r.required)) return Found::Nothing;

	// Checked before asking, so nobody is offered a release that won't install. Required or not,
	// nothing goes in without SHA256SUMS signed by a release key.
	const std::string path = "/ddyfad/bhoptools/releases/download/" + r.tag + "/";
	r.url = "https://github.com" + path;
	std::string sig;
	const bool fetched = fetch("github.com", path + "SHA256SUMS", r.sums) && fetch("github.com", path + "SHA256SUMS.sig", sig);
	why = "couldn't download its SHA256SUMS and SHA256SUMS.sig";
	if (!fetched || !relsig::CheckSums(r.sums, sig, r.tag, asset, r.expected, why)) return Found::Unverified;
	return Found::Offer;
}

// `after` ends the message: what happens to the version running now.
inline std::string UnverifiedText(const Release& r, const std::string& why, const char* after)
{
	return r.tag + " is out, but it couldn't be verified as signed by the bhoptools release key, so it won't be installed:\n\n"
		+ why + "\n\n" + after;
}

inline std::string OfferText(const Release& r)
{
	return r.required
		? "A required update is being installed: " + r.tag + " (you have " APP_VERSION ").\n\nChangelog:\n\n" + r.changelog
		: r.tag + " is out, you have " APP_VERSION ".\n\nChangelog:\n\n" + r.changelog;
}

}
