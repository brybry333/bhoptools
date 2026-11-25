// Checks for the client code that reads untrusted input without the game: the update notice's release tag, the
// lump checksum list and the trigger sim's map entities.
// Build and run: g++ -std=c++14 -Iclient/shared client/tests/checks.cpp -o checks && ./checks
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "lump_checksums.h"
#include "update_check.h"
#include "prediction.h"

static PredMap* BuildMap(const std::string& lump)
{
	PredMap* m = new PredMap();
	PredParseEntityLump(lump.c_str(), lump.size(), m->entities);
	PredBuildTriggers(*m);
	return m;
}

static int SimOf(const PredMap& m, const char* name)
{
	for (int i = 0; i < (int)m.sim.size(); ++i)
		if (!strcmp(m.entities[m.sim[i].entity].Get("targetname"), name)) return i;
	return -1;
}

// Filters are numbered in entity order.
static int FilterOf(const PredMap& m, const char* name)
{
	int fi = 0;
	for (const PredMapEntity& e : m.entities) {
		if (strncmp(e.Get("classname"), "filter_", 7) != 0) continue;
		if (!strcmp(e.Get("targetname"), name)) return fi;
		++fi;
	}
	return -1;
}

static bool Passes(const PredMap& m, const char* filter)
{
	return PredFilterPasses(m, FilterOf(m, filter), "someone", "player");
}

static void CheckTriggerSim()
{
	// filters: the server's short-circuit, Filter01-05 only, non-filters skipped, empty AND/OR, !player
	std::string lump =
		"{ \"classname\" \"filter_activator_name\" \"targetname\" \"pass\" \"filtername\" \"!PLAYER\" }\n"
		"{ \"classname\" \"filter_activator_name\" \"targetname\" \"fail\" \"filtername\" \"nobody\" }\n"
		"{ \"classname\" \"info_target\" \"targetname\" \"target\" }\n"
		"{ \"classname\" \"filter_multi\" \"targetname\" \"and\" \"Filter01\" \"pass\" \"Filter02\" \"fail\" }\n"
		"{ \"classname\" \"filter_multi\" \"targetname\" \"or\" \"FilterType\" \"1\" \"Filter01\" \"fail\" \"Filter02\" \"pass\" }\n"
		"{ \"classname\" \"filter_multi\" \"targetname\" \"six\" \"Filter01\" \"pass\" \"Filter06\" \"fail\" }\n"
		"{ \"classname\" \"filter_multi\" \"targetname\" \"notfilter\" \"Filter01\" \"target\" \"Filter02\" \"pass\" }\n"
		"{ \"classname\" \"filter_multi\" \"targetname\" \"emptyand\" }\n"
		"{ \"classname\" \"filter_multi\" \"targetname\" \"emptyor\" \"FilterType\" \"1\" }\n"
		"{ \"classname\" \"filter_multi\" \"targetname\" \"loop\" \"FilterType\" \"1\" \"Negated\" \"1\" \"Filter01\" \"loop\""
		" \"Filter02\" \"loop\" \"Filter03\" \"loop\" \"Filter04\" \"loop\" \"Filter05\" \"loop\" }\n";
	// Ten levels of OR, each listing the next five times: 5^10 checks without the budget.
	for (int i = 0; i < 10; ++i) {
		char ent[512], next[16];
		snprintf(next, sizeof next, i < 9 ? "n%d" : "fail", i + 1);
		snprintf(ent, sizeof ent, "{ \"classname\" \"filter_multi\" \"targetname\" \"n%d\" \"FilterType\" \"1\" \"Filter01\" \"%s\""
			" \"Filter02\" \"%s\" \"Filter03\" \"%s\" \"Filter04\" \"%s\" \"Filter05\" \"%s\" }\n", i, next, next, next, next, next);
		lump += ent;
	}
	PredMap* m = BuildMap(lump);
	assert(Passes(*m, "pass") && !Passes(*m, "fail"));
	assert(!Passes(*m, "and") && Passes(*m, "or"));
	assert(Passes(*m, "six") && Passes(*m, "notfilter"));
	assert(Passes(*m, "emptyand") && !Passes(*m, "emptyor"));
	assert(!Passes(*m, "loop") && !Passes(*m, "n0"));
	delete m;

	// logic_case compares text; math_counter's start value, min/max order and SetValueNoFire; relay refire delay;
	// fire-limit counters only for limited actions
	m = BuildMap(
		"{ \"classname\" \"logic_case\" \"targetname\" \"c\" \"Case01\" \"red\" \"Case02\" \"1\" \"Case03\" \"2.0\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"targetname\" \"t\" \"model\" \"*1\" \"OnStartTouch\" \"c,InValue,RED\""
		" \"OnStartTouch\" \"c,InValue,blue\" \"OnStartTouch\" \"c,InValue,2\" \"OnStartTouch\" \"c,InValue,2.0\""
		" \"OnStartTouch\" \"c,InValue\" }\n"
		"{ \"classname\" \"math_counter\" \"targetname\" \"m\" \"OutValue\" \"c,InValue\" }\n"
		"{ \"classname\" \"logic_case\" \"targetname\" \"c2\" \"OnDefault\" \"c,InValue\" }\n"
		"{ \"classname\" \"math_counter\" \"targetname\" \"swap\" \"min\" \"10\" \"max\" \"2\" \"startvalue\" \"7.9\" }\n"
		"{ \"classname\" \"math_counter\" \"targetname\" \"clamp\" \"min\" \"3\" \"max\" \"5\" \"startvalue\" \"9\" }\n"
		"{ \"classname\" \"math_counter\" \"targetname\" \"free\" \"startvalue\" \"-4\" }\n"
		"{ \"classname\" \"logic_relay\" \"targetname\" \"r\" \"OnTrigger\" \"nobody,Foo,,5\" \"OnTrigger\" \"c,InValue,red,1.5,1\" }\n");
	const int c = SimOf(*m, "c"), t = SimOf(*m, "t"), mc = SimOf(*m, "m");
	const PredSimEnt& tc = m->sim[t];
	assert(tc.actionCount == 5);
	assert(m->actions[tc.actionStart].caseHit == 0 && m->actions[tc.actionStart + 1].caseHit == -1);
	assert(m->actions[tc.actionStart + 2].caseHit == -1 && m->actions[tc.actionStart + 3].caseHit == 2);
	assert(PredCaseHit(*m, m->sim[c], tc.actionStart + 4, 0.0f) == -1);
	assert(PredCaseHit(*m, m->sim[c], m->sim[mc].actionStart, 1.0f) == 1);
	assert(PredCaseHit(*m, m->sim[c], m->sim[mc].actionStart, 2.0f) == -1);
	assert(m->sim[SimOf(*m, "c2")].actionCount == 0);
	const PredSimEnt& sw = m->sim[SimOf(*m, "swap")];
	assert(sw.counterMin == 2.0f && sw.counterMax == 10.0f && sw.initialCounter == 7.0f);
	assert(m->sim[SimOf(*m, "clamp")].initialCounter == 5.0f);
	assert(m->sim[SimOf(*m, "r")].relayDelay == 5.0f);
	assert(m->actionTimes.size() == 1 && m->actions[m->sim[SimOf(*m, "r")].actionStart].timesSlot == 0);
	PredRuntime rt;
	PredAdopt(rt, m);
	assert(rt.world.actionTimesLeft.size() == 1 && rt.ring[0].actionTimesLeft.size() == 1);
	const int fr = SimOf(*m, "free"), cl = SimOf(*m, "clamp");
	assert(rt.world.ents[fr].counterValue == -4.0f);
	PredAcceptInput(rt, fr, IN_CNT_SETVALUENOFIRE, -3.5f, -1);
	assert(rt.world.ents[fr].counterValue == -3.5f);
	rt.world.ents[fr].disabled = 1;
	PredAcceptInput(rt, fr, IN_CNT_SETVALUENOFIRE, 9.0f, -1);
	assert(rt.world.ents[fr].counterValue == -3.5f);
	PredAcceptInput(rt, cl, IN_CNT_SETVALUENOFIRE, 100.0f, -1);
	assert(rt.world.ents[cl].counterValue == 5.0f);
	PredReset(rt);

	// A trigger or teleport destination that an output changes isn't predicted.
	m = BuildMap(
		"{ \"classname\" \"trigger_teleport\" \"targetname\" \"tp1\" \"model\" \"*1\" \"target\" \"d1\" }\n"
		"{ \"classname\" \"trigger_teleport\" \"targetname\" \"tp2\" \"model\" \"*2\" \"target\" \"d2\" }\n"
		"{ \"classname\" \"trigger_teleport\" \"targetname\" \"tp3\" \"model\" \"*3\" \"target\" \"d1\" }\n"
		"{ \"classname\" \"trigger_teleport\" \"targetname\" \"tp4\" \"model\" \"*4\" \"target\" \"d1\""
		" \"OnStartTouch\" \"!self,AddOutput,target d3\" }\n"
		"{ \"classname\" \"info_teleport_destination\" \"targetname\" \"d1\" \"origin\" \"0 0 64\" }\n"
		"{ \"classname\" \"info_teleport_destination\" \"targetname\" \"d2\" \"origin\" \"0 0 128\" }\n"
		"{ \"classname\" \"logic_relay\" \"OnTrigger\" \"tp1,AddOutput,target d2\" \"OnTrigger\" \"d2,SetParent,train\" }\n");
	assert(m->sim[SimOf(*m, "tp1")].cls == PC_NONE && m->sim[SimOf(*m, "tp2")].cls == PC_NONE);
	assert(m->sim[SimOf(*m, "tp3")].cls == PC_TELEPORT && m->sim[SimOf(*m, "tp4")].cls == PC_NONE);
	delete m;

	// Thousands of same-named relays each firing all of them: past the cap, no trigger prediction.
	lump.clear();
	for (int i = 0; i < 1030; ++i) lump += "{ \"classname\" \"logic_relay\" \"targetname\" \"r\" \"OnTrigger\" \"r,Trigger\" }\n";
	m = BuildMap(lump);
	assert(m->stats.overCap && m->sim.empty() && m->actions.empty());
	delete m;
}

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

	CheckTriggerSim();

	puts("checks: ok");
	return 0;
}
