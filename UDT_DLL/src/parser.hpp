#pragma once


#include "context.hpp"
#include "message.hpp"
#include "tokenizer.hpp"
#include "file_stream.hpp"
#include "linear_allocator.hpp"
#include "parser_plug_in.hpp"
#include "array.hpp"

// For the placement new operator.
#include <new>


struct udtBaseParser;
typedef udtStream* (*udtDemoStreamCreator)(s32 startTime, s32 endTime, udtBaseParser* parser, void* userData);

struct udtBaseParser
{
public:
	struct udtConfigString;

public:
	udtBaseParser();
	~udtBaseParser();

	bool	Init(udtContext* context, udtProtocol::Id protocol);
	void	SetFilePath(const char* filePath); // May return NULL if not reading the input demo from a file.
	void    Reset();
	void	Destroy();

	bool	ParseNextMessage(const udtMessage& inMsg, s32 inServerMessageSequence, u32 fileOffset); // Returns true if should continue parsing.
	void	FinishParsing();

	u32	    GetAllocatedByteCount() const;

	void	AddCut(s32 startTimeMs, s32 endTimeMs, udtDemoStreamCreator streamCreator, void* userData = NULL);
	void    AddPlugIn(udtBaseParserPlugIn* plugIn);

	void                  InsertOrUpdateConfigString(const udtConfigString& cs);
	udtConfigString*      FindConfigStringByIndex(s32 csIndex); // Returns NULL when not found.

private:
	bool                  ParseServerMessage(); // Returns true if should continue parsing.
	bool                  ShouldWriteMessage() const;
	s32                   GetVirtualInputTime() const;
	void                  WriteFirstMessage();
	void                  WriteNextMessage();
	void                  WriteLastMessage();
	void                  WriteGameState();
	void                  ParseCommandString();
	void                  ParseGamestate();
	void                  ParseSnapshot();
	void                  ParsePacketEntities(udtMessage& msg, idClientSnapshotBase* oldframe, idClientSnapshotBase* newframe);
	void                  EmitPacketEntities(idClientSnapshotBase* from, idClientSnapshotBase* to);
	void                  DeltaEntity(udtMessage& msg, idClientSnapshotBase *frame, s32 newnum, idEntityStateBase* old, qbool unchanged);
	char*                 AllocatePermanentString(const char* string, u32 stringLength = 0, u32* outStringLength = NULL); // If stringLength is zero, will invoke strlen.
	void                  ResetForGamestateMessage();

public:
	idEntityStateBase*    GetEntity(s32 idx) const { return (idEntityStateBase*)&_inParseEntities[idx * _protocolSizeOfEntityState]; }
	idEntityStateBase*    GetBaseline(s32 idx) const { return (idEntityStateBase*)&_inEntityBaselines[idx * _protocolSizeOfEntityState]; }
	idClientSnapshotBase* GetClientSnapshot(s32 idx) const { return (idClientSnapshotBase*)&_inSnapshots[idx * _protocolSizeOfClientSnapshot]; }
	
	// You don't have to deallocate the object, but you will have to call the destructor manually yourself.
	template<class T>
	T* CreatePersistentObject()
	{
		return new (_inLinearAllocator.Allocate((u32)sizeof(T))) T;
	}
	
public:
	struct udtCutInfo
	{
		udtDemoStreamCreator StreamCreator;
		udtStream* Stream; // Requires manual destruction.
		void* UserData;
		s32 StartTimeMs;
		s32 EndTimeMs;
	};

	struct udtConfigString
	{
		const char* String;
		u32 StringLength;
		s32 Index;
	};

	struct udtServerCommand
	{
		const char* String;
		u32 StringLength;
	};

	typedef udtVMArray<udtCutInfo> CutVector;
	typedef udtVMArray<udtConfigString> ConfigStringVector;
	typedef udtVMArray<udtServerCommand> ServerCommandVector;
	typedef udtVMArray<u8> EntityVector;
	typedef udtVMArray<u8> SnapshotVector;
	typedef udtVMArray<udtBaseParserPlugIn*> PlugInVector;

public:
	// General.
	udtContext* _context; // This instance does *NOT* have ownership of the context.
	udtProtocol::Id _protocol;
	s32 _protocolSizeOfEntityState;
	s32 _protocolSizeOfClientSnapshot;

	// Callbacks. Useful for doing additional analysis/processing in the same demo reading pass.
	void* UserData; // Put whatever you want in there. Useful for callbacks.
	PlugInVector PlugIns;

	// Input.
	const char* _inFilePath;
	udtMessage _inMsg; // This instance does *NOT* have ownership of the raw message data.
	u32 _inFileOffset;
	s32 _inServerMessageSequence; // Unreliable.
	s32 _inServerCommandSequence; // Reliable.
	s32 _inReliableSequenceAcknowledge;
	s32 _inClientNum;
	s32 _inChecksumFeed;
	s32 _inParseEntitiesNum;
	s32 _inServerTime;
	EntityVector _inEntityBaselines; // Fixed-size array of size MAX_PARSE_ENTITIES. Must be zeroed initially.
	EntityVector _inParseEntities; // Fixed-size array of size MAX_PARSE_ENTITIES.
	ServerCommandVector _inCommands;
	ConfigStringVector _inConfigStrings;
	udtVMLinearAllocator _inLinearAllocator;
	SnapshotVector _inSnapshots; // Fixed-size array of size PACKET_BACKUP.
	idLargestClientSnapshot _inSnapshot;

	// Output.
	CutVector _cuts;
	u8 _outMsgData[MAX_MSGLEN];
	udtMessage _outMsg; // This instance *DOES* have ownership of the raw message data.
	s32 _outServerCommandSequence;
	s32 _outSnapshotsWritten;
	bool _outWriteFirstMessage;
	bool _outWriteMessage;
};