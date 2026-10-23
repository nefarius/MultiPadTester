#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

inline constexpr std::array<std::string_view, 21> kDiagnosticCsvColumns = {
	"steady_us",
	"qpc_us",
	"kind",
	"event",
	"deadline_us",
	"render_start_us",
	"present_start_us",
	"present_end_us",
	"next_deadline_us",
	"inter_present_us",
	"deadline_lateness_us",
	"render_us",
	"present_block_us",
	"early_wakeups",
	"present_hresult",
	"last_present_count",
	"stats_present_count",
	"stats_present_refresh_count",
	"stats_sync_refresh_count",
	"stats_sync_qpc",
	"stats_valid",
};

struct DiagnosticCsvFrame
{
	int64_t steadyUs = 0;
	int64_t qpcUs = 0;
	bool isEvent = false;
	std::string event;
	bool hasDeadline = false;
	int64_t deadlineUs = 0;
	bool hasRenderStart = false;
	int64_t renderStartUs = 0;
	bool hasPresentStart = false;
	int64_t presentStartUs = 0;
	bool hasPresentEnd = false;
	int64_t presentEndUs = 0;
	bool hasNextDeadline = false;
	int64_t nextDeadlineUs = 0;
	bool hasInterPresent = false;
	int64_t interPresentUs = 0;
	bool hasLateness = false;
	int64_t deadlineLatenessUs = 0;
	bool hasRender = false;
	int64_t renderUs = 0;
	bool hasPresentBlock = false;
	int64_t presentBlockUs = 0;
	bool hasEarlyWakeups = false;
	int32_t earlyWakeups = 0;
	bool hasPresentHr = false;
	uint32_t presentHr = 0;
	bool hasLastPresentCount = false;
	uint32_t lastPresentCount = 0;
	bool statsValid = false;
	uint32_t statsPresentCount = 0;
	uint32_t statsPresentRefreshCount = 0;
	uint32_t statsSyncRefreshCount = 0;
	int64_t statsSyncQpc = 0;
};

inline std::string DiagnosticCsvHeader()
{
	std::string header;
	for (size_t i = 0; i < kDiagnosticCsvColumns.size(); ++i)
	{
		if (i != 0)
			header.push_back(',');
		header.append(kDiagnosticCsvColumns[i]);
	}
	return header;
}

inline std::string DiagnosticCsvRow(const DiagnosticCsvFrame& frame)
{
	auto append = [](std::string& out, bool& first, std::string_view value) {
		if (!first)
			out.push_back(',');
		first = false;
		if (value.find_first_of(",\"\r\n") == std::string_view::npos)
		{
			out.append(value);
			return;
		}
		out.push_back('"');
		for (const char ch : value)
		{
			if (ch == '"')
				out.push_back('"');
			out.push_back(ch);
		}
		out.push_back('"');
	};
	auto appendNumber = [&](std::string& out, bool& first, const bool present, const int64_t value) {
		append(out, first, present ? std::to_string(value) : std::string_view{});
	};

	std::string row;
	bool first = true;
	appendNumber(row, first, true, frame.steadyUs);
	appendNumber(row, first, true, frame.qpcUs);
	append(row, first, frame.isEvent ? "event" : "frame");
	append(row, first, frame.event);
	appendNumber(row, first, frame.hasDeadline, frame.deadlineUs);
	appendNumber(row, first, frame.hasRenderStart, frame.renderStartUs);
	appendNumber(row, first, frame.hasPresentStart, frame.presentStartUs);
	appendNumber(row, first, frame.hasPresentEnd, frame.presentEndUs);
	appendNumber(row, first, frame.hasNextDeadline, frame.nextDeadlineUs);
	appendNumber(row, first, frame.hasInterPresent, frame.interPresentUs);
	appendNumber(row, first, frame.hasLateness, frame.deadlineLatenessUs);
	appendNumber(row, first, frame.hasRender, frame.renderUs);
	appendNumber(row, first, frame.hasPresentBlock, frame.presentBlockUs);
	appendNumber(row, first, frame.hasEarlyWakeups, frame.earlyWakeups);
	if (frame.hasPresentHr)
	{
		char hex[16]{};
		std::snprintf(hex, sizeof(hex), "%08X", frame.presentHr);
		append(row, first, hex);
	}
	else
		append(row, first, {});
	appendNumber(row, first, frame.hasLastPresentCount, frame.lastPresentCount);
	appendNumber(row, first, frame.statsValid, frame.statsPresentCount);
	appendNumber(row, first, frame.statsValid, frame.statsPresentRefreshCount);
	appendNumber(row, first, frame.statsValid, frame.statsSyncRefreshCount);
	appendNumber(row, first, frame.statsValid, frame.statsSyncQpc);
	if (frame.isEvent)
		append(row, first, {});
	else
		append(row, first, frame.statsValid ? "1" : "0");
	return row;
}
