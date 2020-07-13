#include "parser.hpp"
#include "file_stream.hpp"
#include "utils.hpp"
#include "shared.hpp"
#include "file_system.hpp"
#include "parser_context.hpp"
#include "timer.hpp"
#include "stack_trace.hpp"
#include "path.hpp"
#include "batch_runner.hpp"

#include <stdio.h>
#include <stdlib.h>


#define    UDT_CUTTER_BATCH_SIZE    256


void PrintHelp()
{
	printf("Cuts demos by time, chat or matches.\n");
	printf("\n");
	printf("UDT_cutter t [-o=outputfolder] [-q] [-g=gamestateindex] -s=starttime -e=endtime inputfile\n");
	printf("UDT_cutter c [-o=outputfolder] [-q] [-t=maxthreads] [-r] -c=configpath inputfile|inputfolder\n");
	printf("UDT_cutter m [-o=outputfolder] [-q] [-t=maxthreads] [-r] [-s=startoffset] [-e=endoffset] inputfile|inputfolder\n");
	printf("UDT_cutter r [-o=outputfolder] [-q] [-t=maxthreads] [-r] [-s=startoffset] [-e=endoffset] [-f=fragcount] [-p=playerindex] inputfile|inputfolder\n");
	printf("UDT_cutter g -c=configpath\n");
	printf("\n");
	printf("t     cut by time\n");
	printf("c     cut by chat\n");
	printf("m     cut by matches\n");
	printf("r     cut by multi-frag rails\n");
	printf("g     generate a cut by chat example config\n");
	printf("-q    quiet mode: no logging to stdout    (default: off)\n");
	printf("-r    enable recursive demo file search   (default: off)\n");
	printf("-o=p  set the output folder path to p     (default: input folder)\n");
	printf("-g=N  set the game state index to N       (default: 0)\n");
	printf("-t=N  set the maximum thread count to N   (default: 1)\n");
	printf("-s=T  set the start cut time/offset to T  (default offset: 10 seconds)\n");
	printf("-e=T  set the end cut time/offset to T    (default offset: 10 seconds)\n");
	printf("-p=P  set the player tracking mode to P   (default: followed player)\n");
	printf("      (0-63: player id, -1: demo taker, -2: followed player)\n");
	printf("-c=p  set the config file path to p\n");
	printf("\n");
	printf("Start and end times/offsets (-s and -e) can be formatted as:\n");
	printf("- 'seconds'          (example: 192)\n");
	printf("- 'minutes:seconds'  (example: 3:12)\n");
	printf("\n");
	printf("In cut by matches...\n");
	printf("- the start offset is only applied if no pre-match countdown is found\n");
	printf("- the end offset is only applied if no post-match intermission is found\n");
}


static const char* ExampleConfig = 
"// Generated by UDT_cutter, feel free to modify :)\n"
"// ChatOperator values: Contains, StartsWith, EndsWith\n"
"\n"
"StartOffset 10\n"
"EndOffset 10\n"
"\n"
"[ChatRule]\n"
"ChatOperator Contains\n"
"Pattern WAXEDDD\n"
"CaseSensitive 1\n"
"IgnoreColorCodes 0\n"
"[/ChatRule]\n"
"\n"
"[ChatRule]\n"
"ChatOperator Contains\n"
"Pattern \"fragmovie frag\"\n"
"CaseSensitive 0\n"
"IgnoreColorCodes 1\n"
"[/ChatRule]\n";


struct CutByChatConfig
{
	udtVMArray<udtChatPatternRule> ChatRules { "CutByChatConfig::ChatRulesArray" };
	udtVMLinearAllocator StringAllocator { "CutByChatConfig::String" };
	const char* CustomOutputFolder = nullptr;
	int MaxThreadCount = 1;
	int StartOffsetSec = 10;
	int EndOffsetSec = 10;
};

struct CutByMultiFragRailConfig
{
	const char* CustomOutputFolder;
	s32 StartOffsetSec = 10;
	s32 EndOffsetSec = 10;
	u32 MaxThreadCount = 1;
	u32 MinFragCount = 2;
	s32 PlayerIndex = udtPlayerIndex::FirstPersonPlayer;
};


static void InitRule(udtChatPatternRule& rule)
{
	memset(&rule, 0, sizeof(rule));
	rule.CaseSensitive = 1;
	rule.ChatOperator = (u32)udtChatOperator::Contains;
	rule.IgnoreColorCodes = 1;
	rule.Pattern = "WAXEDDD";
	rule.SearchTeamChat = 0;
}

static bool CreateConfig(const char* filePath)
{
	udtFileStream file;
	if(!file.Open(filePath, udtFileOpenMode::Write))
	{
		return false;
	}

	return file.Write(ExampleConfig, (u32)strlen(ExampleConfig), 1) == 1;
}

static bool ReadConfig(CutByChatConfig& config, udtContext& context, udtVMLinearAllocator& fileAllocator, const char* filePath)
{
	bool definingRule = false;
	udtChatPatternRule rule;
	InitRule(rule);
	s32 tempInt;

	udtFileStream file;
	if(!file.Open(filePath, udtFileOpenMode::Read))
	{
		return false;
	}

	udtString fileString = file.ReadAllAsString(fileAllocator);
	if(fileString.GetLength() == 0 || !fileString.IsValid())
	{
		return false;
	}
	file.Close();

	udtVMArray<udtString> lines("ReadConfig::LinesArray");
	if(!StringSplitLines(lines, fileString))
	{
		return false;
	}

	idTokenizer& tokenizer = context.Tokenizer;
	for(u32 i = 0, count = lines.GetSize(); i < count; ++i)
	{
		const udtString line = lines[i];
		if(udtString::IsNullOrEmpty(line) || udtString::StartsWith(line, "//"))
		{
			continue;
		}

		if(udtString::Equals(line, "[ChatRule]"))
		{
			definingRule = true;
			InitRule(rule);
			continue;
		}

		if(udtString::Equals(line, "[/ChatRule]"))
		{
			definingRule = false;
			config.ChatRules.Add(rule);
			continue;
		}

		tokenizer.Tokenize(line.GetPtr());
		if(tokenizer.GetArgCount() != 2)
		{
			continue;
		}

		if(definingRule)
		{
			if(udtString::Equals(tokenizer.GetArg(0), "ChatOperator"))
			{
				if(udtString::Equals(tokenizer.GetArg(1), "Contains")) rule.ChatOperator = (u32)udtChatOperator::Contains;
				else if(udtString::Equals(tokenizer.GetArg(1), "StartsWith")) rule.ChatOperator = (u32)udtChatOperator::StartsWith;
				else if(udtString::Equals(tokenizer.GetArg(1), "EndsWith")) rule.ChatOperator = (u32)udtChatOperator::EndsWith;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "Pattern"))
			{
				// We temporarily save an offset because the data might get relocated.
				rule.Pattern = (const char*)(uintptr_t)udtString::NewClone(config.StringAllocator, tokenizer.GetArgString(1)).GetOffset();
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "CaseSensitive"))
			{
				if(udtString::Equals(tokenizer.GetArg(1), "1")) rule.CaseSensitive = 1;
				else if(udtString::Equals(tokenizer.GetArg(1), "0")) rule.CaseSensitive = 0;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "IgnoreColorCodes"))
			{
				if(udtString::Equals(tokenizer.GetArg(1), "1")) rule.IgnoreColorCodes = 1;
				else if(udtString::Equals(tokenizer.GetArg(1), "0")) rule.IgnoreColorCodes = 0;
			}
		}
		else
		{
			if(udtString::Equals(tokenizer.GetArg(0), "StartOffset"))
			{
				if(StringParseInt(tempInt, tokenizer.GetArgString(1)) && tempInt > 0) config.StartOffsetSec = tempInt;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "EndOffset"))
			{
				if(StringParseInt(tempInt, tokenizer.GetArgString(1)) && tempInt > 0) config.EndOffsetSec = tempInt;
			}
		}
	}

	// Fix up the pattern pointers.
	for(u32 i = 0, count = config.ChatRules.GetSize(); i < count; ++i)
	{
		const u32 offset = (u32)(uintptr_t)config.ChatRules[i].Pattern;
		config.ChatRules[i].Pattern = config.StringAllocator.GetStringAt(offset);
	}

	return true;
}

static bool LoadConfig(CutByChatConfig& config, const char* configPath)
{
	udtParserContext* const context = udtCreateContext();
	if(context == NULL)
	{
		fprintf(stderr, "udtCreateContext failed.\n");
		return false;
	}

	udtVMLinearAllocator fileAllocator("LoadConfig::File");
	if(!ReadConfig(config, context->Context, fileAllocator, configPath))
	{
		fprintf(stderr, "Could not load the specified config file.\n");
		return false;
	}

	if(config.ChatRules.IsEmpty())
	{
		fprintf(stderr, "The specified config file doesn't define any chat rule.\n");
		return false;
	}

	return true;
}

static bool CutByTime(const char* filePath, const char* outputFolder, s32 startSec, s32 endSec)
{
	udtParseArg info;
	memset(&info, 0, sizeof(info));
	info.MessageCb = &CallbackConsoleMessage;
	info.ProgressCb = &CallbackConsoleProgress;
	info.OutputFolderPath = outputFolder;
	
	udtCut cut;
	memset(&cut, 0, sizeof(cut));
	cut.StartTimeMs = startSec * 1000;
	cut.EndTimeMs = endSec * 1000;

	udtCutByTimeArg cutInfo;
	memset(&cutInfo, 0, sizeof(cutInfo));
	cutInfo.CutCount = 1;
	cutInfo.Cuts = &cut;

	udtParserContext* const context = udtCreateContext();
	if(context == NULL)
	{
		return false;
	}

	const s32 result = udtCutDemoFileByTime(context, &info, &cutInfo, filePath);
	udtDestroyContext(context);
	if(result == udtErrorCode::None)
	{
		return true;
	}

	fprintf(stderr, "udtCutDemoFileByTime failed with error: %s\n", udtGetErrorCodeString(result));

	return false;
}

static bool CutByChatBatch(udtParseArg& parseArg, const udtFileInfo* files, const u32 fileCount, const CutByChatConfig& config)
{
	udtVMArray<const char*> filePaths("CutByChatMultiple::FilePathsArray");
	udtVMArray<s32> errorCodes("CutByChatMultiple::ErrorCodesArray");
	filePaths.Resize(fileCount);
	errorCodes.Resize(fileCount);
	for(u32 i = 0; i < fileCount; ++i)
	{
		filePaths[i] = files[i].Path.GetPtr();
	}

	udtMultiParseArg threadInfo;
	memset(&threadInfo, 0, sizeof(threadInfo));
	threadInfo.FilePaths = filePaths.GetStartAddress();
	threadInfo.OutputErrorCodes = errorCodes.GetStartAddress();
	threadInfo.FileCount = fileCount;
	threadInfo.MaxThreadCount = (u32)config.MaxThreadCount;

	udtChatPatternArg chatInfo;
	memset(&chatInfo, 0, sizeof(chatInfo));
	chatInfo.Rules = config.ChatRules.GetStartAddress();
	chatInfo.RuleCount = config.ChatRules.GetSize();

	udtPatternInfo patternInfo;
	memset(&patternInfo, 0, sizeof(patternInfo));
	patternInfo.Type = (u32)udtPatternType::Chat;
	patternInfo.TypeSpecificInfo = &chatInfo;

	udtPatternSearchArg patternArg;
	memset(&patternArg, 0, sizeof(patternArg));
	patternArg.StartOffsetSec = (u32)config.StartOffsetSec;
	patternArg.EndOffsetSec = (u32)config.EndOffsetSec;
	patternArg.PatternCount = 1;
	patternArg.Patterns = &patternInfo;

	const s32 result = udtCutDemoFilesByPattern(&parseArg, &threadInfo, &patternArg);

	udtVMLinearAllocator tempAllocator("CutByChatBatch::Temp");
	for(u32 i = 0; i < fileCount; ++i)
	{
		if(errorCodes[i] != (s32)udtErrorCode::None)
		{
			udtString fileName;
			tempAllocator.Clear();
			udtPath::GetFileName(fileName, tempAllocator, udtString::NewConstRef(filePaths[i]));

			fprintf(stderr, "Processing of file %s failed with error: %s\n", fileName.GetPtrSafe("?"), udtGetErrorCodeString(errorCodes[i]));
		}
	}

	if(result == udtErrorCode::None)
	{
		return true;
	}

	fprintf(stderr, "udtCutDemoFilesByPattern failed with error: %s\n", udtGetErrorCodeString(result));

	return false;
}

static bool CutByChatMultipleFiles(udtParseArg& parseArg, const udtFileInfo* files, const u32 fileCount, const CutByChatConfig& config)
{
	parseArg.OutputFolderPath = config.CustomOutputFolder;

	BatchRunner runner(parseArg, files, fileCount, UDT_CUTTER_BATCH_SIZE);
	const u32 batchCount = runner.GetBatchCount();
	for(u32 i = 0; i < batchCount; ++i)
	{
		runner.PrepareNextBatch();
		const BatchRunner::BatchInfo& info = runner.GetBatchInfo(i);
		if(!CutByChatBatch(parseArg, files + info.FirstFileIndex, info.FileCount, config))
		{
			return false;
		}
	}

	return true;
}

static bool CutByChatSingleFile(udtParseArg& parseArg, const char* filePath, const CutByChatConfig& config)
{
	udtFileInfo fileInfo;
	fileInfo.Name = udtString::NewNull();
	fileInfo.Path = udtString::NewConstRef(filePath);
	fileInfo.Size = 0;

	return CutByChatMultipleFiles(parseArg, &fileInfo, 1, config);
}

struct CutByMatchConfig
{
	const char* CustomOutputFolder;
	s32 StartOffsetSec;
	s32 EndOffsetSec;
	u32 MaxThreadCount;
};

static bool CutByMatchBatch(udtParseArg& parseArg, const udtFileInfo* files, const u32 fileCount, const CutByMatchConfig& config)
{
	udtVMArray<const char*> filePaths("CutByChatMultiple::FilePathsArray");
	udtVMArray<s32> errorCodes("CutByChatMultiple::ErrorCodesArray");
	filePaths.Resize(fileCount);
	errorCodes.Resize(fileCount);
	for(u32 i = 0; i < fileCount; ++i)
	{
		filePaths[i] = files[i].Path.GetPtr();
	}

	udtMultiParseArg threadInfo;
	memset(&threadInfo, 0, sizeof(threadInfo));
	threadInfo.FilePaths = filePaths.GetStartAddress();
	threadInfo.OutputErrorCodes = errorCodes.GetStartAddress();
	threadInfo.FileCount = fileCount;
	threadInfo.MaxThreadCount = config.MaxThreadCount;

	udtMatchPatternArg matchInfo;
	memset(&matchInfo, 0, sizeof(matchInfo));
	matchInfo.MatchStartOffsetMs = (u32)config.StartOffsetSec * 1000;
	matchInfo.MatchEndOffsetMs = (u32)config.EndOffsetSec * 1000;

	udtPatternInfo patternInfo;
	memset(&patternInfo, 0, sizeof(patternInfo));
	patternInfo.Type = (u32)udtPatternType::Matches;
	patternInfo.TypeSpecificInfo = &matchInfo;

	udtPatternSearchArg patternArg;
	memset(&patternArg, 0, sizeof(patternArg));
	patternArg.StartOffsetSec = (u32)config.StartOffsetSec;
	patternArg.EndOffsetSec = (u32)config.EndOffsetSec;
	patternArg.PatternCount = 1;
	patternArg.Patterns = &patternInfo;

	const s32 result = udtCutDemoFilesByPattern(&parseArg, &threadInfo, &patternArg);

	udtVMLinearAllocator tempAllocator { "CutByMatchBatch::Temp" };
	for(u32 i = 0; i < fileCount; ++i)
	{
		if(errorCodes[i] != (s32)udtErrorCode::None)
		{
			udtString fileName;
			tempAllocator.Clear();
			udtPath::GetFileName(fileName, tempAllocator, udtString::NewConstRef(filePaths[i]));

			fprintf(stderr, "Processing of file %s failed with error: %s\n", fileName.GetPtrSafe("?"), udtGetErrorCodeString(errorCodes[i]));
		}
	}

	if(result == udtErrorCode::None)
	{
		return true;
	}

	fprintf(stderr, "udtCutDemoFilesByPattern failed with error: %s\n", udtGetErrorCodeString(result));

	return false;
}

static bool CutByMatchMultipleFiles(udtParseArg& parseArg, const udtFileInfo* files, const u32 fileCount, const CutByMatchConfig& config)
{
	parseArg.OutputFolderPath = config.CustomOutputFolder;

	BatchRunner runner(parseArg, files, fileCount, UDT_CUTTER_BATCH_SIZE);
	const u32 batchCount = runner.GetBatchCount();
	for(u32 i = 0; i < batchCount; ++i)
	{
		runner.PrepareNextBatch();
		const BatchRunner::BatchInfo& info = runner.GetBatchInfo(i);
		if(!CutByMatchBatch(parseArg, files + info.FirstFileIndex, info.FileCount, config))
		{
			return false;
		}
	}

	return true;
}

static bool CutByMatchSingleFile(udtParseArg& parseArg, const char* filePath, const CutByMatchConfig& config)
{
	udtFileInfo fileInfo;
	fileInfo.Name = udtString::NewNull();
	fileInfo.Path = udtString::NewConstRef(filePath);
	fileInfo.Size = 0;

	return CutByMatchMultipleFiles(parseArg, &fileInfo, 1, config);
}

static bool CutByMultiFragRailBatch(udtParseArg& parseArg, const udtFileInfo* files, const u32 fileCount, const CutByMultiFragRailConfig& config)
{
	udtVMArray<const char*> filePaths("CutByMultiRailBatch::FilePathsArray");
	udtVMArray<s32> errorCodes("CutByMultiRailBatch::ErrorCodesArray");
	filePaths.Resize(fileCount);
	errorCodes.Resize(fileCount);
	for(u32 i = 0; i < fileCount; ++i)
	{
		filePaths[i] = files[i].Path.GetPtr();
	}

	udtMultiParseArg threadInfo;
	memset(&threadInfo, 0, sizeof(threadInfo));
	threadInfo.FilePaths = filePaths.GetStartAddress();
	threadInfo.OutputErrorCodes = errorCodes.GetStartAddress();
	threadInfo.FileCount = fileCount;
	threadInfo.MaxThreadCount = config.MaxThreadCount;

	udtMultiRailPatternArg multiRailsInfo;
	memset(&multiRailsInfo, 0, sizeof(multiRailsInfo));
	multiRailsInfo.MinKillCount = config.MinFragCount;

	udtPatternInfo patternInfo;
	memset(&patternInfo, 0, sizeof(patternInfo));
	patternInfo.Type = (u32)udtPatternType::MultiFragRails;
	patternInfo.TypeSpecificInfo = &multiRailsInfo;

	udtPatternSearchArg patternArg;
	memset(&patternArg, 0, sizeof(patternArg));
	patternArg.StartOffsetSec = (u32)config.StartOffsetSec;
	patternArg.EndOffsetSec = (u32)config.EndOffsetSec;
	patternArg.PatternCount = 1;
	patternArg.Patterns = &patternInfo;
	patternArg.Flags = udtPatternSearchArgMask::MergeCutSections;
	patternArg.PlayerIndex = config.PlayerIndex;

	const s32 result = udtCutDemoFilesByPattern(&parseArg, &threadInfo, &patternArg);

	udtVMLinearAllocator tempAllocator { "CutByMultiRailBatch::Temp" };
	for(u32 i = 0; i < fileCount; ++i)
	{
		if(errorCodes[i] != (s32)udtErrorCode::None)
		{
			udtString fileName;
			tempAllocator.Clear();
			udtPath::GetFileName(fileName, tempAllocator, udtString::NewConstRef(filePaths[i]));

			fprintf(stderr, "Processing of file %s failed with error: %s\n", fileName.GetPtrSafe("?"), udtGetErrorCodeString(errorCodes[i]));
		}
	}

	if(result == udtErrorCode::None)
	{
		return true;
	}

	fprintf(stderr, "udtCutDemoFilesByPattern failed with error: %s\n", udtGetErrorCodeString(result));

	return false;
}

static bool CutByMultiFragRailMultipleFiles(udtParseArg& parseArg, const udtFileInfo* files, const u32 fileCount, const CutByMultiFragRailConfig& config)
{
	parseArg.OutputFolderPath = config.CustomOutputFolder;

	BatchRunner runner(parseArg, files, fileCount, UDT_CUTTER_BATCH_SIZE);
	const u32 batchCount = runner.GetBatchCount();
	for(u32 i = 0; i < batchCount; ++i)
	{
		runner.PrepareNextBatch();
		const BatchRunner::BatchInfo& info = runner.GetBatchInfo(i);
		if(!CutByMultiFragRailBatch(parseArg, files + info.FirstFileIndex, info.FileCount, config))
		{
			return false;
		}
	}

	return true;
}

static bool CutByMultiFragRailSingleFile(udtParseArg& parseArg, const char* filePath, const CutByMultiFragRailConfig& config)
{
	udtFileInfo fileInfo;
	fileInfo.Name = udtString::NewNull();
	fileInfo.Path = udtString::NewConstRef(filePath);
	fileInfo.Size = 0;

	return CutByMultiFragRailMultipleFiles(parseArg, &fileInfo, 1, config);
}

static bool HasCuttableDemoFileExtension(const udtString& filePath)
{
	for(u32 i = (u32)udtProtocol::FirstCuttableProtocol; i < (u32)udtProtocol::Count; ++i)
	{
		const char* const extension = udtGetFileExtensionByProtocol((udtProtocol::Id)i);
		if(udtString::EndsWithNoCase(filePath, extension))
		{
			return true;
		}
	}

	return false;
}

static bool HasCuttableDemoFileExtension(const char* filePath)
{
	return HasCuttableDemoFileExtension(udtString::NewConstRef(filePath));
}

static bool KeepOnlyCuttableDemoFiles(const char* name, u64 /*size*/, void* /*userData*/)
{
	return HasCuttableDemoFileExtension(name);
}

static const char ValidCommands[] = 
{
	't', 'c', 'm', 'r', 'g'
};

static bool IsValidCommand(char command)
{
	for(u32 i = 0, count = (u32)UDT_COUNT_OF(ValidCommands); i < count; ++i)
	{
		if(command == ValidCommands[i])
		{
			return true;
		}
	}

	return false;
}

struct ProgramOptions
{
	const char* ConfigFilePath = NULL; // -c=
	const char* OutputFolderPath = NULL; // -o=
	u32 MaxThreadCount = 1; // -t=
	u32 GameStateIndex = 0; // -g=
	s32 StartTimeSec = UDT_S32_MIN; // -s=
	s32 EndTimeSec = UDT_S32_MIN; // -e=
	s32 FragCount = UDT_S32_MIN; // -f=
	s32 PlayerIndex = udtPlayerIndex::FirstPersonPlayer; // -p=
	bool Recursive = false;	 // -r
};

static bool LoadChatConfig(CutByChatConfig& config, const ProgramOptions& options)
{
	if(!LoadConfig(config, options.ConfigFilePath))
	{
		fprintf(stderr, "Failed to load the config file.\n");
		return false;
	}

	config.CustomOutputFolder = options.OutputFolderPath;
	config.MaxThreadCount = (int)options.MaxThreadCount;
	if(options.StartTimeSec > 0) config.StartOffsetSec = (int)options.StartTimeSec;
	if(options.EndTimeSec > 0) config.EndOffsetSec = (int)options.EndTimeSec;

	return true;
}

static void LoadMatchConfig(CutByMatchConfig& config, const ProgramOptions& options)
{
	config.CustomOutputFolder = options.OutputFolderPath;
	config.MaxThreadCount = options.MaxThreadCount;
	if(options.StartTimeSec > 0) config.StartOffsetSec = options.StartTimeSec;
	if(options.EndTimeSec > 0) config.EndOffsetSec = options.EndTimeSec;
}

static void LoadMultiFragRailConfig(CutByMultiFragRailConfig& config, const ProgramOptions& options)
{
	config.CustomOutputFolder = options.OutputFolderPath;
	config.MaxThreadCount = options.MaxThreadCount;
	if(options.StartTimeSec > 0) config.StartOffsetSec = options.StartTimeSec;
	if(options.EndTimeSec > 0) config.EndOffsetSec = options.EndTimeSec;
	if(options.FragCount > 1) config.MinFragCount = options.FragCount;
	config.PlayerIndex = options.PlayerIndex;
}


int udt_main(int argc, char** argv)
{
	if(argc < 3)
	{
		PrintHelp();
		return 0;
	}

	const udtString commandString = udtString::NewConstRef(argv[1]);
	if(commandString.GetLength() != 1)
	{
		fprintf(stderr, "Invalid command.\n");
		return 1;
	}

	const char command = commandString.GetPtr()[0];
	if(!IsValidCommand(command))
	{
		fprintf(stderr, "Invalid command.\n");
		return 1;
	}

	const int firstOption = 2;
	const int afterLastOption = command == 'g' ? argc : (argc - 1);

	ProgramOptions options;
	if(command == 'm')
	{
		options.StartTimeSec = 10;
		options.EndTimeSec = 10;
	}

	for(int i = firstOption; i < afterLastOption; ++i)
	{
		s32 localInt = 0;

		const udtString arg = udtString::NewConstRef(argv[i]);
		if(udtString::Equals(arg, "-r"))
		{
			options.Recursive = true;
		}
		else if(udtString::StartsWith(arg, "-c=") &&
				arg.GetLength() >= 4)
		{
			options.ConfigFilePath = argv[i] + 3;
		}
		else if(udtString::StartsWith(arg, "-o=") &&
				arg.GetLength() >= 4)
		{
			options.OutputFolderPath = argv[i] + 3;
		}
		else if(udtString::StartsWith(arg, "-t=") &&
				arg.GetLength() >= 4 &&
				StringParseInt(localInt, arg.GetPtr() + 3) &&
				localInt >= 1 &&
				localInt <= 16)
		{
			options.MaxThreadCount = (u32)localInt;
		}
		else if(udtString::StartsWith(arg, "-g=") &&
				arg.GetLength() >= 4 &&
				StringParseInt(localInt, arg.GetPtr() + 3) &&
				localInt >= 0)
		{
			options.GameStateIndex = (u32)localInt;
		}
		else if(udtString::StartsWith(arg, "-s=") &&
				arg.GetLength() >= 4 &&
				StringParseSeconds(localInt, arg.GetPtr() + 3))
		{
			options.StartTimeSec = localInt;
		}
		else if(udtString::StartsWith(arg, "-e=") &&
				arg.GetLength() >= 4 &&
				StringParseSeconds(localInt, arg.GetPtr() + 3))
		{
			options.EndTimeSec = localInt;
		}
		else if(udtString::StartsWith(arg, "-f=") &&
				arg.GetLength() >= 4 &&
				StringParseInt(localInt, arg.GetPtr() + 3) &&
				localInt >= 2 &&
				localInt <= 8)
		{
			options.FragCount = localInt;
		}
		else if(udtString::StartsWith(arg, "-p=") &&
				arg.GetLength() >= 4 &&
				StringParseInt(localInt, arg.GetPtr() + 3) &&
				localInt >= -2 &&
				localInt <= 63)
		{
			options.PlayerIndex = localInt;
		}
	}

	if(command == 'g')
	{
		if(options.ConfigFilePath == NULL)
		{
			fprintf(stderr, "The config file path was not specified.\n");
			return 1;
		}

		return CreateConfig(options.ConfigFilePath) ? 0 : 1;
	}

	const char* const inputPath = argv[argc - 1];
	bool fileMode = false;
	if(udtFileStream::Exists(inputPath) && HasCuttableDemoFileExtension(inputPath))
	{
		fileMode = true;
	}
	else if(!IsValidDirectory(inputPath))
	{
		fprintf(stderr, "Invalid file/folder path.\n");
		return 1;
	}

	if(command == 't')
	{
		if(!fileMode)
		{
			fprintf(stderr, "The input path must be a file, not a folder.\n");
			return 1;
		}

		if(options.StartTimeSec == UDT_S32_MIN)
		{
			fprintf(stderr, "The start time was not specified.\n");
			return 1;
		}

		if(options.EndTimeSec == UDT_S32_MIN)
		{
			fprintf(stderr, "The end time was not specified.\n");
			return 1;
		}

		return CutByTime(inputPath, options.OutputFolderPath, options.StartTimeSec, options.EndTimeSec) ? 0 : 1;
	}

	if(command == 'c' && options.ConfigFilePath == NULL)
	{
		fprintf(stderr, "The config file path was not specified.\n");
		return 1;
	}

	CmdLineParseArg parseArg;
	if(fileMode)
	{
		if(command == 'c')
		{
			CutByChatConfig config;
			if(!LoadChatConfig(config, options))
			{
				return 1;
			}

			return CutByChatSingleFile(parseArg.ParseArg, inputPath, config) ? 0 : 1;
		}
		else if(command == 'm')
		{
			CutByMatchConfig config;
			LoadMatchConfig(config, options);

			return CutByMatchSingleFile(parseArg.ParseArg, inputPath, config) ? 0 : 1;
		}
		else if(command == 'r')
		{
			CutByMultiFragRailConfig config;
			LoadMultiFragRailConfig(config, options);

			return CutByMultiFragRailSingleFile(parseArg.ParseArg, inputPath, config) ? 0 : 1;
		}
	}
	else
	{
		udtFileListQuery query;
		query.FileFilter = &KeepOnlyCuttableDemoFiles;
		query.FolderPath = udtString::NewConstRef(inputPath);
		query.Recursive = options.Recursive;
		query.UserData = NULL;
		GetDirectoryFileList(query);

		if(command == 'c')
		{
			CutByChatConfig config;
			if(!LoadChatConfig(config, options))
			{
				return 1;
			}

			return CutByChatMultipleFiles(parseArg.ParseArg, query.Files.GetStartAddress(), query.Files.GetSize(), config) ? 0 : 1;
		}
		else if(command == 'm')
		{
			CutByMatchConfig config;
			LoadMatchConfig(config, options);

			return CutByMatchMultipleFiles(parseArg.ParseArg, query.Files.GetStartAddress(), query.Files.GetSize(), config) ? 0 : 1;
		}
		else if(command == 'r')
		{
			CutByMultiFragRailConfig config;
			LoadMultiFragRailConfig(config, options);

			return CutByMultiFragRailMultipleFiles(parseArg.ParseArg, query.Files.GetStartAddress(), query.Files.GetSize(), config) ? 0 : 1;
		}
	}

	return 0;
}
