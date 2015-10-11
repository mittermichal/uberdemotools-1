#include "parser.hpp"
#include "shared.hpp"
#include "stack_trace.hpp"
#include "utils.hpp"

#include <stdio.h>
#include <stdlib.h>


static void PrintHelp()
{
	printf("\n");
	printf("UDT_timeshifter snapshot_count demo_path_1 [demo_path_2 ... demo_path_n]\n");
	printf("where snapshot_count is in the range: [1;8]\n");
}

static int TimeShiftDemos(s32 snapshotCount, char** filePaths, int fileCount)
{
	udtVMArrayWithAlloc<s32> errorCodes;
	errorCodes.Init((uptr)sizeof(s32) * (uptr)fileCount, "TimeShiftDemos::ErrorCodesArray");
	errorCodes.Resize((u32)fileCount);

	s32 cancel = 0;

	udtParseArg info;
	memset(&info, 0, sizeof(info));
	info.CancelOperation = &cancel;
	info.MessageCb = &CallbackConsoleMessage;
	info.ProgressCb = &CallbackConsoleProgress;

	udtMultiParseArg extraInfo;
	memset(&extraInfo, 0, sizeof(extraInfo));
	extraInfo.FileCount = (u32)fileCount;
	extraInfo.FilePaths = (const char**)filePaths;
	extraInfo.OutputErrorCodes = errorCodes.GetStartAddress();

	udtTimeShiftArg timeShiftArg;
	memset(&timeShiftArg, 0, sizeof(timeShiftArg));
	timeShiftArg.SnapshotCount = snapshotCount;

	const s32 errorCode = udtTimeShiftDemoFiles(&info, &extraInfo, &timeShiftArg);
	if(errorCode != (s32)udtErrorCode::None)
	{
		printf("Error: %s\n", udtGetErrorCodeString(errorCode));
		return __LINE__;
	}

	return 0;
}

int udt_main(int argc, char** argv)
{
	if(argc < 3)
	{
		PrintHelp();
		return __LINE__;
	}

	s32 snapshotCount = -1;
	if(!StringParseSeconds(snapshotCount, argv[1]) || snapshotCount <= 0 || snapshotCount > 8)
	{
		printf("Invalid snapshot count.\n");
		PrintHelp();
		return __LINE__;
	}

	return TimeShiftDemos(snapshotCount, argv + 2, argc - 2);
}
