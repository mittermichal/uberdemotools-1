#pragma once


#include "types.hpp"


extern void* VirtualMemoryReserve(uptr byteCount);
extern bool  VirtualMemoryCommit(void* address, uptr byteCount);
extern bool  VirtualMemoryDecommit(void* address, uptr byteCount);
extern bool  VirtualMemoryDecommitAndRelease(void* address, uptr byteCount);
