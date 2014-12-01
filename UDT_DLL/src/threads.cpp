#include "threads.hpp"


#if defined(UDT_WINDOWS)
#	include <Windows.h>
#else
#	include <pthread.h>
#	include <stdlib.h>
#endif


#if defined(UDT_WINDOWS)

DWORD WINAPI GlobalThreadCallback(LPVOID lpThreadParameter)
{
	udtThread* const thread = (udtThread*)lpThreadParameter;
	if(thread == NULL)
	{
		return 1;
	}

	thread->InvokeUserFunction();

	return 0;
}

#else

void* GlobalThreadCallback(void* threadParameter)
{
	udtThread* const thread = (udtThread*)threadParameter;
	if(thread == NULL)
	{
		return (void*)1;
	}

	thread->InvokeUserFunction();

	return (void*)0;
}

#endif


udtThread::udtThread()
{
	_threadhandle = NULL;
	_userData = NULL;
	_entryPoint = NULL;
}

udtThread::~udtThread()
{
	Release();
}

bool udtThread::CreateAndStart(ThreadEntryPoint entryPoint, void* userData)
{
	if(_threadhandle != NULL)
	{
		return false;
	}

	_entryPoint = entryPoint;
	_userData = userData;

#if defined(UDT_WINDOWS)

	const HANDLE thread = CreateThread(NULL, 0, &GlobalThreadCallback, this, 0, NULL);
	_threadhandle = thread;

	return thread != NULL;

#else

	pthread_t thread;
	pthread_attr_t attribs;
	pthread_attr_init(&attribs);
	pthread_attr_setdetachstate(&attribs, PTHREAD_CREATE_JOINABLE);
	const bool success = pthread_create(&thread, &attribs, &GlobalThreadCallback, this) == 0;
	pthread_attr_destroy(&attribs);
	if(success)
	{
		_threadhandle = (u8*)malloc(sizeof(pthread_t));
		memcpy(_threadhandle, &thread, sizeof(pthread_t));
	}

	return success;

#endif
}

bool udtThread::Join()
{
	if(_threadhandle == NULL)
	{
		return false;
	}
	
#if defined(UDT_WINDOWS)
	return WaitForSingleObject((HANDLE)_threadhandle, INFINITE) != WAIT_OBJECT_0;
#else
	return pthread_join((pthread_t*)_threadhandle, NULL) == 0;
#endif
}

void udtThread::Release()
{
	if(_threadhandle != NULL)
	{
#if defined(UDT_WINDOWS)
		CloseHandle((HANDLE)_threadhandle);
#else
		free(_threadhandle);
#endif

		_threadhandle = NULL;
	}
}

void udtThread::InvokeUserFunction()
{
	if(_entryPoint != NULL)
	{
		(*_entryPoint)(_userData);
	}
}