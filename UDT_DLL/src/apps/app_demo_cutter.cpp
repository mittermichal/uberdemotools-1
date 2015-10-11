#include "parser.hpp"
#include "file_stream.hpp"
#include "utils.hpp"
#include "shared.hpp"
#include "file_system.hpp"
#include "parser_context.hpp"
#include "timer.hpp"
#include "stack_trace.hpp"
#include "path.hpp"

#include <stdio.h>
#include <stdlib.h>


static const char* ConfigFilePath = "udt_cutter.cfg";

static const char* DefaultConfig = 
"// Generated by UDT_cutter, feel free to modify :)\n"
"// ChatOperator values: Contains, StartsWith, EndsWith\n"
"\n"
"StartOffset 10\n"
"EndOffset 10\n"
"RecursiveSearch 0\n"
"UseCustomOutputFolder 0\n"
"CustomOutputFolder \"\"\n"
"MaxThreadCount 4\n"
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
	CutByChatConfig()
	{
		MaxThreadCount = 4;
		StartOffsetSec = 10;
		EndOffsetSec = 10;
		RecursiveSearch = false;
		UseCustomOutputFolder = false;
		CustomOutputFolder = NULL;
		ChatRules.Init(1 << 16, "CutByChatConfig::ChatRulesArray");
	}

	int MaxThreadCount;
	int StartOffsetSec;
	int EndOffsetSec;
	bool RecursiveSearch;
	bool UseCustomOutputFolder;
	const char* CustomOutputFolder;
	udtVMArrayWithAlloc<udtCutByChatRule> ChatRules;
};


static void InitRule(udtCutByChatRule& rule)
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

	return file.Write(DefaultConfig, (u32)strlen(DefaultConfig), 1) == 1;
}

static bool EnsureConfigExists(const char* filePath)
{
	udtFileStream file;
	if(!file.Open(filePath, udtFileOpenMode::Read))
	{
		if(!CreateConfig(filePath))
		{
			return false;
		}

		if(!file.Open(filePath, udtFileOpenMode::Read))
		{
			return false;
		}
	}

	return true;
}

static bool ReadConfig(CutByChatConfig& config, udtContext& context, udtVMLinearAllocator& fileAllocator, udtVMLinearAllocator& persistAllocator, const char* filePath)
{
	if(!EnsureConfigExists(filePath))
	{
		return false;
	}

	bool definingRule = false;
	udtCutByChatRule rule;
	InitRule(rule);
	s32 tempInt;

	udtFileStream file;
	if(!file.Open(filePath, udtFileOpenMode::Read))
	{
		return false;
	}

	char* const fileData = file.ReadAllAsString(fileAllocator);
	if(fileData == NULL)
	{
		return false;
	}

	const u32 fileLength = (u32)file.Length();
	file.Close();

	idTokenizer& tokenizer = context.Tokenizer;

	udtVMArrayWithAlloc<udtString> lines(1 << 16, "ReadConfig::LinesArray");
	udtString fileString = udtString::NewRef(fileData, fileLength, fileLength + 1);
	if(!StringSplitLines(lines, fileString))
	{
		return false;
	}

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

		tokenizer.Tokenize(line.String);
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
				rule.Pattern = AllocateString(persistAllocator, tokenizer.GetArgString(1));
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "CaseSensitive"))
			{
				if(udtString::Equals(tokenizer.GetArg(1), "1")) rule.CaseSensitive = true;
				else if(udtString::Equals(tokenizer.GetArg(1), "0")) rule.CaseSensitive = false;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "IgnoreColorCodes"))
			{
				if(udtString::Equals(tokenizer.GetArg(1), "1")) rule.IgnoreColorCodes = true;
				else if(udtString::Equals(tokenizer.GetArg(1), "0")) rule.IgnoreColorCodes = false;
			}
		}
		else
		{
			if(udtString::Equals(tokenizer.GetArg(0), "MaxThreadCount"))
			{
				if(StringParseInt(tempInt, tokenizer.GetArgString(1)) && tempInt > 0) config.MaxThreadCount = tempInt;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "StartOffset"))
			{
				if(StringParseInt(tempInt, tokenizer.GetArgString(1))) config.StartOffsetSec = tempInt;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "EndOffset"))
			{
				if(StringParseInt(tempInt, tokenizer.GetArgString(1))) config.EndOffsetSec = tempInt;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "RecursiveSearch"))
			{
				if(udtString::Equals(tokenizer.GetArg(1), "1")) config.RecursiveSearch = true;
				else if(udtString::Equals(tokenizer.GetArg(1), "0")) config.RecursiveSearch = false;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "UseCustomOutputFolder"))
			{
				if(udtString::Equals(tokenizer.GetArg(1), "1")) config.UseCustomOutputFolder = true;
				else if(udtString::Equals(tokenizer.GetArg(1), "0")) config.UseCustomOutputFolder = false;
			}
			else if(udtString::Equals(tokenizer.GetArg(0), "CustomOutputFolder") && IsValidDirectory(tokenizer.GetArgString(1)))
			{
				config.CustomOutputFolder = AllocateString(persistAllocator, tokenizer.GetArgString(1));
			}
		}
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
	cut.StartTimeMs = startSec * 1000;
	cut.EndTimeMs = endSec * 1000;

	udtCutByTimeArg cutInfo;
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

static bool CutByChatMultiple(const udtFileInfo* files, const u32 fileCount, const CutByChatConfig& config)
{
	udtVMArrayWithAlloc<const char*> filePaths(1 << 16, "CutByChatMultiple::FilePathsArray");
	udtVMArrayWithAlloc<s32> errorCodes(1 << 16, "CutByChatMultiple::ErrorCodesArray");
	filePaths.Resize(fileCount);
	errorCodes.Resize(fileCount);
	for(u32 i = 0; i < fileCount; ++i)
	{
		filePaths[i] = files[i].Path;
	}

	udtParseArg info;
	memset(&info, 0, sizeof(info));
	s32 cancelOperation = 0;
	info.CancelOperation = &cancelOperation;
	info.MessageCb = &CallbackConsoleMessage;
	info.ProgressCb = &CallbackConsoleProgress;
	info.OutputFolderPath = config.UseCustomOutputFolder ? config.CustomOutputFolder : NULL;

	udtMultiParseArg threadInfo;
	memset(&threadInfo, 0, sizeof(threadInfo));
	threadInfo.FilePaths = filePaths.GetStartAddress();
	threadInfo.OutputErrorCodes = errorCodes.GetStartAddress();
	threadInfo.FileCount = fileCount;
	threadInfo.MaxThreadCount = (u32)config.MaxThreadCount;

	udtCutByChatArg chatInfo;
	chatInfo.Rules = config.ChatRules.GetStartAddress();
	chatInfo.RuleCount = config.ChatRules.GetSize();

	udtPatternInfo patternInfo;
	patternInfo.Type = (u32)udtPatternType::Chat;
	patternInfo.TypeSpecificInfo = &chatInfo;

	udtCutByPatternArg patternArg;
	patternArg.StartOffsetSec = config.StartOffsetSec;
	patternArg.EndOffsetSec = config.EndOffsetSec;
	patternArg.PatternCount = 1;
	patternArg.Patterns = &patternInfo;
	patternArg.PlayerIndex = -999;
	patternArg.PlayerName = NULL;

	const s32 result = udtCutDemoFilesByPattern(&info, &threadInfo, &patternArg);
	
	udtVMLinearAllocator tempAllocator;
	tempAllocator.Init(1 << 16, "CutByChatMultiple::Temp");
	for(u32 i = 0; i < fileCount; ++i)
	{
		if(errorCodes[i] != (s32)udtErrorCode::None)
		{
			udtString fileName;
			tempAllocator.Clear();
			udtPath::GetFileName(fileName, tempAllocator, udtString::NewConstRef(filePaths[i]));

			fprintf(stderr, "Processing of file %s failed with error: %s\n", fileName.String != NULL ? fileName.String : "?", udtGetErrorCodeString(errorCodes[i]));
		}
	}

	if(result == udtErrorCode::None)
	{
		return true;
	}

	fprintf(stderr, "udtCutDemoFilesByPattern failed with error: %s\n", udtGetErrorCodeString(result));

	return false;
}

static bool CutByChat(const char* filePath, const CutByChatConfig& config)
{
	udtFileInfo fileInfo;
	fileInfo.Name = NULL; // Ignored by CutByChatMultiple.
	fileInfo.Size = 0; // Ignored by CutByChatMultiple.
	fileInfo.Path = filePath;

	return CutByChatMultiple(&fileInfo, 1, config);
}

static void PrintHelp()
{
	printf("???? help for UDT_cutter ????\n");
	printf("Syntax: UDT_cutter path [start_time end_time]\n");
	printf("UDT_cutter demo_file_path start_time end_time   <-- cut a single demo by time\n");
	printf("UDT_cutter demo_file_path   <-- cut a single demo by chat\n");
	printf("UDT_cutter demo_folder_path   <-- cut a bunch of demos by chat\n");
	printf("If the start and end times aren't specified, will do \"Cut by Chat\"\n");
	printf("Start and end times can either be written as 'seconds' or 'minutes:seconds'\n");
	printf("All the rules/variables are read from the config file udt_cutter.cfg\n");
}

static bool KeepOnlyDemoFiles(const char* name, u64 /*size*/)
{
	return udtPath::HasValidDemoFileExtension(name);
}

int udt_main(int argc, char** argv)
{
	EnsureConfigExists(ConfigFilePath);

	udtParserContext* const context = udtCreateContext();
	if(context == NULL)
	{
		return __LINE__;
	}

	CutByChatConfig config;
	udtVMLinearAllocator configAllocator;
	udtVMLinearAllocator fileAllocator;
	configAllocator.Init(1 << 24, "udt_main::Config");
	fileAllocator.Init(1 << 16, "udt_main::File");
	if(!ReadConfig(config, context->Context, configAllocator, fileAllocator, ConfigFilePath))
	{
		printf("Could not load config file.\n");
		return __LINE__;
	}

	if(argc != 2 && argc != 4)
	{
		printf("Wrong argument count.\n");
		PrintHelp();
		return __LINE__;
	}

	if(argc == 2)
	{
		printf("Two arguments, enabling cut by chat.\n");

		bool fileMode = false;
		if(udtFileStream::Exists(argv[1]) && udtPath::HasValidDemoFileExtension(argv[1]))
		{
			fileMode = true;
		}
		else if(!IsValidDirectory(argv[1]))
		{
			printf("Invalid file/folder path.\n");
			PrintHelp();
			return __LINE__;
		}

		if(fileMode)
		{
			printf("\n");
			return CutByChat(argv[1], config) ? 0 : 666;
		}

		udtVMArrayWithAlloc<udtFileInfo> files(1 << 16, "udt_main::FilesArray");
		udtVMLinearAllocator persistAlloc;
		udtVMLinearAllocator tempAlloc;
		persistAlloc.Init(1 << 24, "udt_main::Persistent");
		tempAlloc.Init(1 << 24, "udt_main::Temp");

		udtFileListQuery query;
		memset(&query, 0, sizeof(query));
		query.FileFilter = &KeepOnlyDemoFiles;
		query.Files = &files;
		query.FolderPath = argv[1];
		query.PersistAllocator = &persistAlloc;
		query.Recursive = false;
		query.TempAllocator = &tempAlloc;
		GetDirectoryFileList(query);

		udtTimer timer;
		timer.Start();
		if(!CutByChatMultiple(files.GetStartAddress(), files.GetSize(), config))
		{
			return 999;
		}

		timer.Stop();
		u64 totalByteCount = 0;
		for(u32 i = 0, count = files.GetSize(); i < count; ++i)
		{
			totalByteCount += files[i].Size;
		}
		printf("Batch processing time: %d ms\n", (int)timer.GetElapsedMs());
		const f64 elapsedSec = (f64)timer.GetElapsedMs() / 1000.0;
		const f64 megs = (f64)totalByteCount / (f64)(1 << 20);
		printf("Throughput: %.1f MB/s\n", (float)(megs / elapsedSec));

		return 0;
	}

	printf("Four arguments, enabling cut by time.\n");

	if(!udtFileStream::Exists(argv[1]) || !udtPath::HasValidDemoFileExtension(argv[1]))
	{
		printf("Invalid file path.\n");
		PrintHelp();
		return __LINE__;
	}

	s32 startSec = -1;
	if(!StringParseSeconds(startSec, argv[2]))
	{
		printf("Invalid start time-stamp.\n");
		PrintHelp();
		return __LINE__;
	}

	s32 endSec = -1;
	if(!StringParseSeconds(endSec, argv[3]))
	{
		printf("Invalid end time-stamp.\n");
		PrintHelp();
		return __LINE__;
	}

	printf("\n");

	return CutByTime(argv[1], config.UseCustomOutputFolder ? config.CustomOutputFolder : NULL, startSec, endSec) ? 0 : 666;
}
