#pragma once
#include <stdint.h>

using SrFixtureDispatch = void (*)(void*, const void*, const void*, uint32_t, const char* const*);
extern "C" void SrFixtureWithCommand(const char*, int, SrFixtureDispatch, void*);
extern "C" void SrFixtureWithArguments(uint32_t, const char* const*, int, SrFixtureDispatch, void*);
extern "C" int SrFixtureCaller(const void*);

extern "C" void* SrFixtureEngine(void (*disconnect)(int, unsigned, const char*, void*), void* data);

extern "C" bool SrEngineReadListening(int receiver, int sender);
extern "C" bool SrEngineWriteListening(void* instance, int receiver, int sender, bool value);
extern "C" void SrEngineFailListening(bool fail);
extern "C" unsigned SrEngineListeningCalls();

extern "C" unsigned SrEngineMapChanges();
extern "C" const char* SrEngineChangedMap();
extern "C" bool SrNetworkInitialize(const char* path);
extern "C" void* SrNetworkInterface(const char* name);
extern "C" void* SrNetworkGameEventManager();
extern "C" const char* SrNetworkMenuText();
extern "C" bool SrNetworkStop();
