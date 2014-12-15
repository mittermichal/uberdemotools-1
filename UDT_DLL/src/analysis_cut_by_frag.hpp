#pragma once


#include "analysis_obituaries.hpp"


struct udtCutByFragAnalyzer
{
public:
	struct CutSection
	{
		s32 GameStateIndex;
		s32 StartTimeMs;
		s32 EndTimeMs;
	};

	typedef udtVMArray<CutSection> CutSectionVector;

public:
	udtCutByFragAnalyzer(const udtCutByFragArg& info) 
		: _info(info)
	{
		_protocol = udtProtocol::Invalid;
	}

	~udtCutByFragAnalyzer()
	{
	}

	void ProcessGamestateMessage(const udtGamestateCallbackArg& arg, udtBaseParser& parser)
	{
		_protocol = parser._protocol;
		_analyzer.ProcessGamestateMessage(arg, parser);
	}

	void ProcessCommandMessage(const udtCommandCallbackArg& arg, udtBaseParser& parser)
	{
		_analyzer.ProcessCommandMessage(arg, parser);
	}

	void ProcessSnapshotMessage(const udtSnapshotCallbackArg& arg, udtBaseParser& parser)
	{
		_analyzer.ProcessSnapshotMessage(arg, parser);
	}

	void FindCutSections();

	CutSectionVector CutSections;

private:
	UDT_NO_COPY_SEMANTICS(udtCutByFragAnalyzer);

	void AddCurrentSectionIfValid();
	void AddMatch(const udtParseDataObituary& data);

	struct Frag
	{
		s32 GameStateIndex;
		s32 ServerTimeMs;
	};

	const udtCutByFragArg& _info;
	udtVMArray<Frag> _frags;
	udtObituariesAnalyzer _analyzer;
	udtProtocol::Id _protocol;
};

struct udtParserPlugInCutByFrag : udtBaseParserPlugIn
{
public:
	udtParserPlugInCutByFrag(const udtCutByFragArg& info) : Analyzer(info)
	{
	}

	~udtParserPlugInCutByFrag()
	{
	}

	void ProcessGamestateMessage(const udtGamestateCallbackArg& arg, udtBaseParser& parser)
	{
		Analyzer.ProcessGamestateMessage(arg, parser);
	}

	void ProcessCommandMessage(const udtCommandCallbackArg& arg, udtBaseParser& parser)
	{
		Analyzer.ProcessCommandMessage(arg, parser);
	}

	void ProcessSnapshotMessage(const udtSnapshotCallbackArg& info, udtBaseParser& parser)
	{
		Analyzer.ProcessSnapshotMessage(info, parser);
	}

	void FinishAnalysis()
	{
		Analyzer.FindCutSections();
	}

	u32 GetElementCount() const 
	{ 
		return Analyzer.CutSections.GetSize();
	}

	u32 GetElementSize() const 
	{
		return (u32)sizeof(udtCutByFragAnalyzer::CutSection);
	};

	void* GetFirstElementAddress() 
	{
		return Analyzer.CutSections.GetStartAddress();
	}

	udtCutByFragAnalyzer Analyzer;

private:
	UDT_NO_COPY_SEMANTICS(udtParserPlugInCutByFrag);
};
