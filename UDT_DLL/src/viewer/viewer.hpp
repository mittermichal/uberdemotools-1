#pragma once


#include "uberdemotools.h"
#include "shared.hpp"
#include "platform.hpp"
#include "array.hpp"
#include "timer.hpp"
#include "demo.hpp"
#include "sprites.hpp"
#include "string.hpp"
#include "gui.hpp"
#include "shared.hpp"
#include "math_2d.hpp"
#include "nanovg/nanovg.h"


struct Platform;
struct PlatformReadOnly;
struct PlatformReadWrite;


extern const f32 ViewerClearColor[4];

#define TAB_LIST(N) \
	N(Options, "Options") \
	N(Chat, "Chat") \
	N(HeatMaps, "Heat Maps") \
	N(Log, "Log")

struct Config
{
	f32 StaticZScale = 0.0f;
	f32 DynamicZScale = 0.4f;
};

#define ITEM(Enum, String) Enum,
struct Tab
{
	enum Id
	{
		TAB_LIST(ITEM)
		Count
	};
};
#undef ITEM

struct Viewer
{
	static void DemoProgressCallback(f32 progress, void* userData);
	static void DemoLoadThreadEntryPoint(void* userData);

	Viewer(Platform& platform);
	~Viewer();

	bool Init(int argc, char** argv);
	void ProcessEvent(const Event& event);
	void Render(RenderParams& renderParams);

private:
	UDT_NO_COPY_SEMANTICS(Viewer);

	struct SpriteDrawParams
	{
		f32 X;
		f32 Y;
		f32 ImageScale;
		f32 CoordsScale;
	};

	struct AppState
	{
		enum Id
		{
			Normal,
			LoadingDemo,
			UploadingMapTexture,
			Count
		};
	};

	bool LoadMapAliases();
	bool LoadSprites();
	void LoadMap(const udtString& mapName);
	bool CreateTextureRGBA(int& textureId, u32 width, u32 height, const u8* pixels);
	void StartLoadingDemo(const char* filePath);
	void LoadDemoImpl(const char* filePath);
	void RenderDemo(RenderParams& renderParams);
	void RenderNoDemo(RenderParams& renderParams);
	void RenderDemoLoadProgress(RenderParams& renderParams);
	void RenderDemoScore(RenderParams& renderParams);
	void RenderDemoTimer(RenderParams& renderParams);
	void RenderDemoFollowedPlayer(RenderParams& renderParams);
	void DrawProgressSliderToolTip(RenderParams& renderParams);
	void DrawHelp(RenderParams& renderParams);
	void DrawMapSpriteAt(const SpriteDrawParams& params, u32 spriteId, const f32* pos, f32 size, f32 zScale, f32 a = 0.0f);
	u32  GetCurrentSnapshotIndex();
	u32  GetSapshotIndexFromTime(u32 elapsedMs);
	f32  GetProgressFromTime(u32 elapsedMs);
	u32  GetTimeFromProgress(f32 progress);
	void PausePlayback();
	void ResumePlayback();
	void TogglePlayback();
	void StopPlayback();
	void ReversePlayback();
	void SetPlaybackProgress(f32 progress);
	void OnKeyPressed(VirtualKey::Id virtualKeyId, bool repeat);
	void OffsetSnapshot(s32 snapshotCount);
	void OffsetTimeMs(s32 durationMs);
	void ComputeMapPosition(f32* result, const f32* input, f32 mapScale, f32 zScale);
	void UploadMapTexture();

	struct MapAlias
	{
		udtString NameFound;
		udtString NameToUse;
	};

	struct DemoLoadThreadData
	{
		char FilePath[512];
		Viewer* ViewerPtr;
	};

	int _sprites[Sprite::Count];
	udtVMLinearAllocator _tempAllocator;
	udtVMLinearAllocator _persistAllocator;
	udtVMArray<MapAlias> _mapAliases;
	Demo _demo;
	Config _config;
	udtTimer _demoPlaybackTimer;
	udtTimer _globalTimer;
	DemoProgressBar _demoProgressBar;
	PlayPauseButton _playPauseButton;
	StopButton _stopButton;
	ReverseButton _reversePlaybackButton;
	CheckBox _showServerTimeCheckBox;
	CheckBox _drawMapOverlaysCheckBox;
	CheckBox _drawMapScoresCheckBox;
	CheckBox _drawMapClockCheckBox;
	CheckBox _drawMapFollowMsgCheckBox;
	CheckBox _drawMapHealthCheckBox;
	WidgetGroup _activeWidgets;
	WidgetGroup _tabWidgets[Tab::Count];
	RadioButton _tabButtons[Tab::Count];
	RadioGroup _tabButtonGroup;
	f32 _mapMin[3];
	f32 _mapMax[3];
	Rectangle _mapRect;
	Rectangle _uiRect;
	Rectangle _tabBarRect;
	Rectangle _progressRect;
	Platform& _platform;
	PlatformReadOnly* _sharedReadOnly = nullptr;
	PlatformReadWrite* _sharedReadWrite = nullptr;
	Snapshot* _snapshot = nullptr;
	AppState::Id _appState = AppState::Normal;
	CriticalSectionId _appStateLock = InvalidCriticalSectionId;
	void* _mapImage = nullptr;
	WidgetGroup* _activeTabWidgets = nullptr;
	int _map = InvalidTextureId;
	u32 _mapWidth = 0;
	u32 _mapHeight = 0;
	u32 _snapshotIndex = 0; // Index of the currently displayed snapshot.
	u32 _selectedTabIndex = 0;
	f32 _demoLoadProgress = 0.0f;
	bool _appPaused = false;
	bool _wasTimerRunningBeforePause = false;
	bool _wasPlayingBeforeProgressDrag = false;
	bool _reversePlayback = false;
	bool _mapCoordsLoaded = false;
	bool _timerShowsServerTime = false;
	bool _appLoaded = false;
	bool _displayHelp = false;
	bool _drawMapOverlays = true;
	bool _drawMapScores = true;
	bool _drawMapClock = true;
	bool _drawMapFollowMsg = true;
	bool _drawMapHealth = true;
};
