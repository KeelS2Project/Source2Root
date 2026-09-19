#pragma once

#include <eiface.h>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>

class EngineFixture final : public IVEngineServer2 {
public:
    std::function<void(CPlayerSlot, ENetworkDisconnectionReason, const char*)> disconnect;
    std::map<std::pair<int,int>,bool> listening;
    bool fail_listening = false;
    unsigned listening_calls = 0, map_changes = 0;
    std::string changed_map;
    bool Connect(CreateInterfaceFn factory) override {
        std::abort();
    }

    void Disconnect() override {
        std::abort();
    }

    void* QueryInterface(const char* pInterfaceName) override {
        std::abort();
    }

    InitReturnVal_t Init() override {
        std::abort();
    }

    void Shutdown() override {
        std::abort();
    }

    void PreShutdown() override {
        std::abort();
    }

    const AppSystemInfo_t* GetDependencies() override {
        std::abort();
    }

    AppSystemTier_t GetTier() override {
        std::abort();
    }

    void Reconnect(CreateInterfaceFn factory, const char* pInterfaceName) override {
        std::abort();
    }

    bool IsSingleton() override {
        std::abort();
    }

    BuildType_t GetBuildType() override {
        std::abort();
    }

    bool IsPaused() override {
        std::abort();
    }

    float GetTimescale(void) const override {
        std::abort();
    }

    void* FindOrCreateWorldSession(const char* pszWorldName, CResourceManifestPrerequisite*) override {
        std::abort();
    }

    CEntityLump* GetEntityLumpForTemplate(const char*, bool, const char*, const char*) override {
        std::abort();
    }

    uint32 GetStatsAppID() const override {
        std::abort();
    }

    void* UnknownFunc1(const char* pszFilename, void* pUnknown1, void* pUnknown2, void* pUnknown3) override {
        std::abort();
    }

    void UnknownFunc2() override {
        std::abort();
    }

    EUniverse GetSteamUniverse() const override {
        std::abort();
    }

    void unk001() override {
        std::abort();
    }

    void unk002() override {
        std::abort();
    }

    void unk003() override {
        std::abort();
    }

    void unk004() override {
        std::abort();
    }

    void unk005() override {
        std::abort();
    }

    void unk006() override {
        std::abort();
    }

    void SetFrameTimeAmnesty(const char* amnesty, int, float frametime) override {
        std::abort();
    }

    const char* GetFrameTimeAmnesty(bool check_cvar) override {
        std::abort();
    }

    void unk101() override {
        std::abort();
    }

    void ShowFrameTimeReport(void*, bool) override {
        std::abort();
    }

    void DumpNetStats(void*, void*) override {
        std::abort();
    }

    void unk201() override {
        std::abort();
    }

    void unk202() override {
        std::abort();
    }

    void ChangeLevel(const char* s1, const char* s2) override {
        if (s2 || !IsMapValid(s1))
            std::abort();

        changed_map = s1;
        ++map_changes;
    }

    int IsMapValid(const char* filename) override {
        return std::string(filename) == "de_dust2" || std::string(filename) == "de_mirage";
    }

    bool IsDedicatedServer(void) override {
        std::abort();
    }

    bool IsHLTVRelay(void) override {
        std::abort();
    }

    bool IsServerLocalOnly(void) override {
        std::abort();
    }

    int PrecacheGeneric(const char* s, bool preload = false) override {
        std::abort();
    }

    bool IsGenericPrecached(char const* s) const override {
        std::abort();
    }

    CPlayerUserId GetPlayerUserId(CPlayerSlot nSlot) override {
        std::abort();
    }

    const char* GetPlayerNetworkIDString(CPlayerSlot nSlot) override {
        std::abort();
    }

    INetChannelInfo* GetPlayerNetInfo(CPlayerSlot nSlot) override {
        std::abort();
    }

    bool IsUserIDInUse(int userID) override {
        std::abort();
    }

    int GetLoadingProgressForUserID(int userID) override {
        std::abort();
    }

    void Message_DetermineMulticastRecipients(bool usepas, const Vector& origin, CPlayerBitVec& playerbits) override {
        std::abort();
    }

    void ServerCommand(const char* str) override {
        std::abort();
    }

    void ClientCommand(CPlayerSlot nSlot, const char* szFmt, ...) override {
        std::abort();
    }

    void ClientPrintf(CPlayerSlot nSlot, const char* szMsg) override {
        std::abort();
    }

    bool IsLowViolence() override {
        std::abort();
    }

    bool SetHLTVChatBan(int tvslot, bool bBanned) override {
        std::abort();
    }

    bool IsAnyClientLowViolence() override {
        std::abort();
    }

    void GetGameDir(CBufferString& gameDir) override {
        std::abort();
    }

    CPlayerSlot CreateFakeClient(const char* netname) override {
        std::abort();
    }

    const char* GetClientConVarValue(CPlayerSlot nSlot, const char* name) override {
        std::abort();
    }

    void LogPrint(const char* msg) override {
        std::abort();
    }

    bool IsLogEnabled() override {
        std::abort();
    }

    bool IsSplitScreenPlayer(CPlayerSlot nSlot) override {
        std::abort();
    }

    edict_t* GetSplitScreenPlayerAttachToEdict(CPlayerSlot nSlot) override {
        std::abort();
    }

    int GetNumSplitScreenUsersAttachedToEdict(CPlayerSlot nSlot) override {
        std::abort();
    }

    edict_t* GetSplitScreenPlayerForEdict(CPlayerSlot nSlot, int nSplitScreenSlot) override {
        std::abort();
    }

    void UnloadSpawnGroup(SpawnGroupHandle_t spawnGroup, int) override {
        std::abort();
    }

    void SetSpawnGroupDescription(SpawnGroupHandle_t spawnGroup, const char* pszDescription) override {
        std::abort();
    }

    bool IsSpawnGroupLoaded(SpawnGroupHandle_t spawnGroup) const override {
        std::abort();
    }

    bool IsSpawnGroupLoading(SpawnGroupHandle_t spawnGroup) const override {
        std::abort();
    }

    void MakeSpawnGroupActive(SpawnGroupHandle_t spawnGroup) override {
        std::abort();
    }

    void SynchronouslySpawnGroup(SpawnGroupHandle_t spawnGroup) override {
        std::abort();
    }

    void SynchronizeAndBlockUntilLoaded(SpawnGroupHandle_t spawnGroup) override {
        std::abort();
    }

    void SetTimescale(float flTimescale) override {
        std::abort();
    }

    uint32 GetAppID() override {
        std::abort();
    }

    const CSteamID* GetClientSteamID(CPlayerSlot nSlot) override {
        std::abort();
    }

    void SetGamestatsData(CGamestatsData* pGamestatsData) override {
        std::abort();
    }

    CGamestatsData* GetGamestatsData() override {
        std::abort();
    }

    void ClientCommandKeyValues(CPlayerSlot nSlot, KeyValues* pCommand) override {
        std::abort();
    }

    void SetDedicatedServerBenchmarkMode(bool bBenchmarkMode) override {
        std::abort();
    }

    bool IsClientFullyAuthenticated(CPlayerSlot nSlot) override {
        std::abort();
    }

    CGlobalVars* GetServerGlobals() override {
        std::abort();
    }

    void SetFakeClientConVarValue(CPlayerSlot nSlot, const char* cvar, const char* value) override {
        std::abort();
    }

    CSharedEdictChangeInfo* GetSharedEdictChangeInfo() override {
        std::abort();
    }

    void SetAchievementMgr(IAchievementMgr* pAchievementMgr) override {
        std::abort();
    }

    IAchievementMgr* GetAchievementMgr() override {
        std::abort();
    }

    bool GetPlayerInfo(CPlayerSlot nSlot, google::protobuf::Message& info) override {
        std::abort();
    }

    uint64 GetClientXUID(CPlayerSlot nSlot) override {
        std::abort();
    }

    void* GetPVSForSpawnGroup(SpawnGroupHandle_t spawnGroup) override {
        std::abort();
    }

    SpawnGroupHandle_t FindSpawnGroupByName(const char* szName) override {
        std::abort();
    }

    CSteamID GetGameServerSteamID() override {
        std::abort();
    }

    int GetBuildVersion(void) const override {
        std::abort();
    }

    bool IsClientLowViolence(CPlayerSlot nSlot) override {
        std::abort();
    }

    void DisconnectClient(CPlayerSlot nSlot,
                          ENetworkDisconnectionReason reason,
                          const char* szInternalReason = nullptr) override {
        disconnect(nSlot, reason, szInternalReason);
    }

    void unk301() override {
        std::abort();
    }

    void unk302() override {
        std::abort();
    }

    bool GetClientListening(CPlayerSlot iReceiver, CPlayerSlot iSender) override {
        auto it = listening.find({iReceiver.Get(), iSender.Get()});
        return it == listening.end() || it->second;
    }

    bool SetClientListening(CPlayerSlot iReceiver, CPlayerSlot iSender, bool bListen) override {
        ++listening_calls;

        if (fail_listening)
            return false;

        listening[{iReceiver.Get(), iSender.Get()}] = bListen;
        return true;
    }

    bool SetClientProximity(CPlayerSlot iReceiver, CPlayerSlot iSender, bool bUseProximity) override {
        std::abort();
    }

    void unk401() override {
        std::abort();
    }

    void unk402() override {
        std::abort();
    }

    void unk403() override {
        std::abort();
    }

    void KickClient(CPlayerSlot nSlot, const char* szInternalReason, ENetworkDisconnectionReason reason) override {
        std::abort();
    }

    void BanClient(CPlayerSlot nSlot, float flDuration, bool bKick) override {
        std::abort();
    }

    void BanClient(CSteamID steamId, float flDuration, bool bKick) override {
        std::abort();
    }

    void unk500() override {
        std::abort();
    }

    void unk501() override {
        std::abort();
    }

    void unk502() override {
        std::abort();
    }

    void unk503() override {
        std::abort();
    }

    void unk504() override {
        std::abort();
    }

    void unk505() override {
        std::abort();
    }

    void unk506() override {
        std::abort();
    }

    void unk507() override {
        std::abort();
    }

    void SetClientUpdateRate(CPlayerSlot nSlot, float flUpdateRate) override {
        std::abort();
    }

    void unk600() override {
        std::abort();
    }

    void unk601() override {
        std::abort();
    }

    void unk602() override {
        std::abort();
    }

    void unk603() override {
        std::abort();
    }

    void unk604() override {
        std::abort();
    }

    void unk605() override {
        std::abort();
    }

    void unk606() override {
        std::abort();
    }

    void unk607() override {
        std::abort();
    }

    void unk608() override {
        std::abort();
    }

    void unk609() override {
        std::abort();
    }
};
