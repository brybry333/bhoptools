// Small fixed-size logs keyed by command number, shared by the predictors that replay commands
// (style keys, autosync, noclip, restart). No allocation: they're filled on the game thread.
#pragma once

// Server reports in command order. A report for a command at or before the newest replaces the
// ones it overtook; a full list drops its oldest. Returns the slot for the new report.
template <class Report, int N>
inline Report& CmdReportInsert(Report (&reports)[N], int& count, int cmd)
{
	while (count > 0 && reports[count - 1].cmd >= cmd) --count;
	if (count == N) {
		for (int i = 1; i < N; ++i) reports[i - 1] = reports[i];
		--count;
	}
	return reports[count++];
}

// A record per command key, oldest first.
template <class Record, int N>
inline Record* KeyedFind(Record (&log)[N], int count, int key)
{
	for (int i = 0; i < count; ++i) if (log[i].key == key) return &log[i];
	return nullptr;
}

// Appends a record, dropping the oldest when full (a replay never reaches back that far).
template <class Record, int N>
inline Record& KeyedPush(Record (&log)[N], int& count)
{
	if (count == N) {
		for (int i = 1; i < N; ++i) log[i - 1] = log[i];
		--count;
	}
	return log[count++];
}
