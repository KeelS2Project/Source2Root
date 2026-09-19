#include "engine_fixture.h"
#include "host_fixture.h"

namespace { EngineFixture engine; }

extern "C" void* SrFixtureEngine(void (*disconnect)(int, unsigned, const char*, void*), void* data) {
    engine.disconnect = [disconnect, data](CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* text) {
        if (reason != NETWORK_DISCONNECT_KICKED)
            std::abort();

        disconnect(slot.Get(), static_cast<unsigned>(reason), text, data);
    };
    return &engine;
}

extern "C" bool SrEngineReadListening(int receiver, int sender) {
    return engine.GetClientListening(CPlayerSlot(receiver), CPlayerSlot(sender));
}

extern "C" bool SrEngineWriteListening(void* instance, int receiver, int sender, bool value) {
    return static_cast<IVEngineServer2*>(instance)->SetClientListening(CPlayerSlot(receiver), CPlayerSlot(sender), value);
}

extern "C" void SrEngineFailListening(bool fail) {
    engine.fail_listening = fail;
}

extern "C" unsigned SrEngineListeningCalls() {
    return engine.listening_calls;
}

extern "C" unsigned SrEngineMapChanges() {
    return engine.map_changes;
}

extern "C" const char* SrEngineChangedMap() {
    return engine.changed_map.c_str();
}
