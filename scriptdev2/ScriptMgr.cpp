/* Copyright (C) 2006 - 2012 ScriptDev2 <http://www.scriptdev2.com/>
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#include "../../../game/ScriptMgr.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ProgressBar.h"
#include "config.h"
#include "precompiled.h"
#include "../system/ScriptLoader.h"
#include "../system/system.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "maps/visitors.h"

typedef std::vector<Script*> SDScriptVec;
int num_sc_scripts;
SDScriptVec m_scripts;

Config SD2Config;

void FillSpellSummary();

void LoadDatabase()
{
    std::string strSD2DBinfo =
        SD2Config.GetStringDefault("ScriptDev2DatabaseInfo", "");

    if (strSD2DBinfo.empty())
    {
        logging.error(
            "SD2: Missing Scriptdev2 database info from configuration file. "
            "Load database aborted.");
        return;
    }

    // Initialize connection to DB
    if (SD2Database.Initialize(strSD2DBinfo.c_str()))
    {
        logging.info("SD2: ScriptDev2 database initialized.\n");

        pSystemMgr.LoadScriptTexts();
        pSystemMgr.LoadScriptTextsCustom();
        pSystemMgr.LoadScriptGossipTexts();
        pSystemMgr.LoadScriptWaypoints();
    }
    else
    {
        logging.error(
            "SD2: Unable to connect to Database. Load database aborted.");
        return;
    }

    SD2Database.HaltDelayThread();
}

struct TSpellSummary
{
    uint8 Targets; // set of enum SelectTarget
    uint8 Effects; // set of enum SelectEffect
} extern* SpellSummary;

MANGOS_DLL_EXPORT
void FreeScriptLibrary()
{
    // Free Spell Summary
    delete[] SpellSummary;

    // Free resources before library unload (FIXME: deleting scripts crashes the
    // core)
    // for (SDScriptVec::const_iterator itr = m_scripts.begin(); itr !=
    // m_scripts.end(); ++itr)
    // delete *itr;

    m_scripts.clear();

    num_sc_scripts = 0;
}

MANGOS_DLL_EXPORT
void InitScriptLibrary()
{
    // ScriptDev2 startup
    logging.info(
        "\n\n"
        " MMM  MMM    MM\n"
        "M  MM M  M  M  M\n"
        "MM    M   M   M\n"
        " MMM  M   M  M\n"
        "   MM M   M MMMM\n"
        "MM  M M  M \n"
        " MMM  MMM\n"
        "http://www.scriptdev2.com\n");

    // Get configuration file
    if (!SD2Config.SetSource(_SCRIPTDEV2_CONFIG))
        logging.error(
            "SD2: Unable to open configuration file. Database will be "
            "unaccessible. Configuration values will use default.");
    else
        logging.info("SD2: Using configuration file %s", _SCRIPTDEV2_CONFIG);

    // Check config file version
    if (SD2Config.GetIntDefault("ConfVersion", 0) != SD2_CONF_VERSION)
        logging.error(
            "SD2: Configuration file version doesn't match expected version. "
            "Some config variables may be wrong or missing.");

    // Load database (must be called after SD2Config.SetSource).
    LoadDatabase();

    logging.info("SD2: Loading C++ scripts");

    // Resize script ids to needed ammount of assigned ScriptNames (from core)
    m_scripts.resize(GetScriptIdsCount(), NULL);

    FillSpellSummary();

    AddScripts();

    // Check existance scripts for all registered by core script names
    BarGoLink bar(GetScriptIdsCount());
    bar.step();
    for (uint32 i = 1; i < GetScriptIdsCount(); ++i)
    {
        bar.step();
        if (!m_scripts[i])
            logging.error(
                "SD2: No script found for ScriptName '%s'.", GetScriptName(i));
    }

    logging.info("Loaded %i C++ Scripts.\n", num_sc_scripts);
}

//*********************************
//*** Functions used globally ***

/**
 * Function that does script text
 *
 * @param iTextEntry Entry of the text, stored in SD2-database
 * @param pSource Source of the text
 * @param pTarget Can be NULL (depending on CHAT_TYPE of iTextEntry). Possible
 *target for the text
 */
void DoScriptText(int32 iTextEntry, WorldObject* pSource, Unit* pTarget)
{
    if (!pSource)
    {
        logging.error(
            "SD2: DoScriptText entry %i, invalid Source pointer.", iTextEntry);
        return;
    }

    if (iTextEntry >= 0)
    {
        logging.error(
            "SD2: DoScriptText with source entry %u (TypeId=%u, guid=%u) "
            "attempts to process text entry %i, but text entry must be "
            "negative.",
            pSource->GetEntry(), pSource->GetTypeId(), pSource->GetGUIDLow(),
            iTextEntry);

        return;
    }

    const StringTextData* pData = pSystemMgr.GetTextData(iTextEntry);
    if (!pData)
    {
        logging.error(
            "SD2: DoScriptText with source entry %u (TypeId=%u, guid=%u) could "
            "not find text entry %i.",
            pSource->GetEntry(), pSource->GetTypeId(), pSource->GetGUIDLow(),
            iTextEntry);

        return;
    }

    LOG_DEBUG(logging,
        "SD2: DoScriptText: text entry=%i, Sound=%u, Type=%u, Language=%u, "
        "Emote=%u",
        iTextEntry, pData->uiSoundId, pData->uiType, pData->uiLanguage,
        pData->uiEmote);

    if (pData->uiSoundId)
    {
        if (GetSoundEntriesStore()->LookupEntry(pData->uiSoundId))
        {
            if (pData->uiType == CHAT_TYPE_ZONE_YELL)
                pSource->GetMap()->PlayDirectSoundToMap(
                    pData->uiSoundId, pSource->GetZoneId());
            else if (pData->uiType == CHAT_TYPE_WHISPER ||
                     pData->uiType == CHAT_TYPE_BOSS_WHISPER)
            {
                // An error will be displayed for the text
                if (pTarget && pTarget->GetTypeId() == TYPEID_PLAYER)
                    pSource->PlayDirectSound(
                        pData->uiSoundId, (Player*)pTarget);
            }
            else
                pSource->PlayDirectSound(pData->uiSoundId);
        }
        else
            logging.error(
                "SD2: DoScriptText entry %i tried to process invalid sound id "
                "%u.",
                iTextEntry, pData->uiSoundId);
    }

    if (pData->uiEmote)
    {
        if (pSource->GetTypeId() == TYPEID_UNIT ||
            pSource->GetTypeId() == TYPEID_PLAYER)
            ((Unit*)pSource)->HandleEmote(pData->uiEmote);
        else
            logging.error(
                "SD2: DoScriptText entry %i tried to process emote for invalid "
                "TypeId (%u).",
                iTextEntry, pSource->GetTypeId());
    }

    switch (pData->uiType)
    {
    case CHAT_TYPE_SAY:
        pSource->MonsterSay(iTextEntry, pData->uiLanguage, pTarget);
        break;
    case CHAT_TYPE_YELL:
        pSource->MonsterYell(iTextEntry, pData->uiLanguage, pTarget);
        break;
    case CHAT_TYPE_TEXT_EMOTE:
        pSource->MonsterTextEmote(iTextEntry, pTarget);
        break;
    case CHAT_TYPE_BOSS_EMOTE:
        pSource->MonsterTextEmote(iTextEntry, pTarget, true);
        break;
    case CHAT_TYPE_WHISPER:
    {
        if (pTarget && pTarget->GetTypeId() == TYPEID_PLAYER)
            pSource->MonsterWhisper(iTextEntry, pTarget);
        else
            logging.error(
                "SD2: DoScriptText entry %i cannot whisper without target unit "
                "(TYPEID_PLAYER).",
                iTextEntry);

        break;
    }
    case CHAT_TYPE_BOSS_WHISPER:
    {
        if (pTarget && pTarget->GetTypeId() == TYPEID_PLAYER)
            pSource->MonsterWhisper(iTextEntry, pTarget, true);
        else
            logging.error(
                "SD2: DoScriptText entry %i cannot whisper without target unit "
                "(TYPEID_PLAYER).",
                iTextEntry);

        break;
    }
    case CHAT_TYPE_ZONE_YELL:
        pSource->MonsterYellToZone(iTextEntry, pData->uiLanguage, pTarget);
        break;
    }
}

/**
 * Function that either simulates or does script text for a map
 *
 * @param iTextEntry Entry of the text, stored in SD2-database, only type
 *CHAT_TYPE_ZONE_YELL supported
 * @param uiCreatureEntry Id of the creature of whom saying will be simulated
 * @param pMap Given Map on which the map-wide text is displayed
 * @param pCreatureSource Can be NULL. If pointer to Creature is given, then the
 *creature does the map-wide text
 * @param pTarget Can be NULL. Possible target for the text
 */
void DoOrSimulateScriptTextForMap(int32 iTextEntry, uint32 uiCreatureEntry,
    Map* pMap, Creature* pCreatureSource /*=NULL*/, Unit* pTarget /*=NULL*/)
{
    if (!pMap)
    {
        logging.error(
            "SD2: DoOrSimulateScriptTextForMap entry %i, invalid Map pointer.",
            iTextEntry);
        return;
    }

    if (iTextEntry >= 0)
    {
        logging.error(
            "SD2: DoOrSimulateScriptTextForMap with source entry %u for map %u "
            "attempts to process text entry %i, but text entry must be "
            "negative.",
            uiCreatureEntry, pMap->GetId(), iTextEntry);
        return;
    }

    CreatureInfo const* pInfo = GetCreatureTemplateStore(uiCreatureEntry);
    if (!pInfo)
    {
        logging.error(
            "SD2: DoOrSimulateScriptTextForMap has invalid source entry %u for "
            "map %u.",
            uiCreatureEntry, pMap->GetId());
        return;
    }

    const StringTextData* pData = pSystemMgr.GetTextData(iTextEntry);
    if (!pData)
    {
        logging.error(
            "SD2: DoOrSimulateScriptTextForMap with source entry %u for map %u "
            "could not find text entry %i.",
            uiCreatureEntry, pMap->GetId(), iTextEntry);
        return;
    }

    LOG_DEBUG(logging,
        "SD2: DoOrSimulateScriptTextForMap: text entry=%i, Sound=%u, Type=%u, "
        "Language=%u, Emote=%u",
        iTextEntry, pData->uiSoundId, pData->uiType, pData->uiLanguage,
        pData->uiEmote);

    if (pData->uiType != CHAT_TYPE_ZONE_YELL)
    {
        logging.error(
            "SD2: DoSimulateScriptTextForMap entry %i has not supported chat "
            "type %u.",
            iTextEntry, pData->uiType);
        return;
    }

    if (pData->uiSoundId)
    {
        if (GetSoundEntriesStore()->LookupEntry(pData->uiSoundId))
            pMap->PlayDirectSoundToMap(pData->uiSoundId);
        else
            logging.error(
                "SD2: DoOrSimulateScriptTextForMap entry %i tried to process "
                "invalid sound id %u.",
                iTextEntry, pData->uiSoundId);
    }

    if (pCreatureSource) // If provided pointer for sayer, use direct version
        pMap->MonsterYellToMap(pCreatureSource->GetObjectGuid(), iTextEntry,
            pData->uiLanguage, pTarget);
    else // Simulate yell
        pMap->MonsterYellToMap(pInfo, iTextEntry, pData->uiLanguage, pTarget);
}

void DoAggroEnemyInRange(
    Creature* source, float dist, bool ignore_los, bool visible_only)
{
    if (source->IsInEvadeMode())
        return;

    auto set = maps::visitors::yield_set<Unit, Player, Creature, Pet,
        TemporarySummon>{}(source, dist,
        [source, ignore_los, visible_only](Unit* u)
        {
            return u->isAlive() && u->IsHostileTo(source) &&
                   (u->GetTypeId() != TYPEID_PLAYER ||
                       !static_cast<Player*>(u)->isGameMaster()) &&
                   source->CanStartAttacking(u) &&
                   (ignore_los || source->IsWithinLOSInMap(u)) &&
                   (!visible_only || u->can_be_seen_by(source, source));
        });

    // Pick closest
    std::sort(set.begin(), set.end(), [source](auto* a, auto* b)
        {
            return G3D::distance(source->GetX() - a->GetX(),
                       source->GetY() - a->GetY(), source->GetZ() - a->GetZ()) <
                   G3D::distance(source->GetX() - b->GetX(),
                       source->GetY() - b->GetY(), source->GetZ() - b->GetZ());
        });

    if (!set.empty())
        source->AI()->AttackStart(set[0]);
}

//*********************************
//*** Functions used internally ***

void Script::RegisterSelf(bool bReportError)
{
    if (uint32 id = GetScriptId(Name.c_str()))
    {
        m_scripts[id] = this;
        ++num_sc_scripts;
    }
    else
    {
        if (bReportError)
            logging.error(
                "SD2: Script registering but ScriptName %s is not assigned in "
                "database. Script will not be used.",
                Name.c_str());

        delete this;
    }
}

//********************************
//*** Functions to be Exported ***

MANGOS_DLL_EXPORT
bool GossipHello(Player* pPlayer, Creature* pCreature)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pGossipHello)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGossipHello(pPlayer, pCreature);
}

MANGOS_DLL_EXPORT
bool GOGossipHello(Player* pPlayer, GameObject* pGo)
{
    Script* pTempScript = m_scripts[pGo->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pGossipHelloGO)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGossipHelloGO(pPlayer, pGo);
}

MANGOS_DLL_EXPORT
bool GossipSelect(
    Player* pPlayer, Creature* pCreature, uint32 uiSender, uint32 uiAction)
{
    LOG_DEBUG(logging, "SD2: Gossip selection, sender: %u, action: %u",
        uiSender, uiAction);

    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pGossipSelect)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGossipSelect(pPlayer, pCreature, uiSender, uiAction);
}

MANGOS_DLL_EXPORT
bool GOGossipSelect(
    Player* pPlayer, GameObject* pGo, uint32 uiSender, uint32 uiAction)
{
    LOG_DEBUG(logging, "SD2: GO Gossip selection, sender: %u, action: %u",
        uiSender, uiAction);

    Script* pTempScript = m_scripts[pGo->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pGossipSelectGO)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGossipSelectGO(pPlayer, pGo, uiSender, uiAction);
}

MANGOS_DLL_EXPORT
bool GossipSelectWithCode(Player* pPlayer, Creature* pCreature, uint32 uiSender,
    uint32 uiAction, const char* sCode)
{
    LOG_DEBUG(logging,
        "SD2: Gossip selection with code, sender: %u, action: %u", uiSender,
        uiAction);

    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pGossipSelectWithCode)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGossipSelectWithCode(
        pPlayer, pCreature, uiSender, uiAction, sCode);
}

MANGOS_DLL_EXPORT
bool GOGossipSelectWithCode(Player* pPlayer, GameObject* pGo, uint32 uiSender,
    uint32 uiAction, const char* sCode)
{
    LOG_DEBUG(logging,
        "SD2: GO Gossip selection with code, sender: %u, action: %u", uiSender,
        uiAction);

    Script* pTempScript = m_scripts[pGo->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pGossipSelectGOWithCode)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGossipSelectGOWithCode(
        pPlayer, pGo, uiSender, uiAction, sCode);
}

MANGOS_DLL_EXPORT
bool QuestAccept(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pQuestAcceptNPC)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pQuestAcceptNPC(pPlayer, pCreature, pQuest);
}

MANGOS_DLL_EXPORT
bool QuestRewarded(Player* pPlayer, Creature* pCreature, Quest const* pQuest)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pQuestRewardedNPC)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pQuestRewardedNPC(pPlayer, pCreature, pQuest);
}

MANGOS_DLL_EXPORT
uint32 GetNPCDialogStatus(Player* pPlayer, Creature* pCreature)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pDialogStatusNPC)
        return 100;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pDialogStatusNPC(pPlayer, pCreature);
}

MANGOS_DLL_EXPORT
uint32 GetGODialogStatus(Player* pPlayer, GameObject* pGo)
{
    Script* pTempScript = m_scripts[pGo->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pDialogStatusGO)
        return 100;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pDialogStatusGO(pPlayer, pGo);
}

MANGOS_DLL_EXPORT
bool ItemQuestAccept(Player* pPlayer, Item* pItem, Quest const* pQuest)
{
    Script* pTempScript = m_scripts[pItem->GetProto()->ScriptId];

    if (!pTempScript || !pTempScript->pQuestAcceptItem)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pQuestAcceptItem(pPlayer, pItem, pQuest);
}

MANGOS_DLL_EXPORT
bool GOUse(Player* pPlayer, GameObject* pGo)
{
    Script* pTempScript = m_scripts[pGo->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pGOUse)
        return false;

    return pTempScript->pGOUse(pPlayer, pGo);
}

MANGOS_DLL_EXPORT
bool GOQuestAccept(Player* pPlayer, GameObject* pGo, const Quest* pQuest)
{
    Script* pTempScript = m_scripts[pGo->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pQuestAcceptGO)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pQuestAcceptGO(pPlayer, pGo, pQuest);
}

MANGOS_DLL_EXPORT
bool GOQuestRewarded(Player* pPlayer, GameObject* pGo, Quest const* pQuest)
{
    Script* pTempScript = m_scripts[pGo->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pQuestRewardedGO)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pQuestRewardedGO(pPlayer, pGo, pQuest);
}

MANGOS_DLL_EXPORT
bool AreaTrigger(Player* pPlayer, AreaTriggerEntry const* atEntry)
{
    Script* pTempScript = m_scripts[GetAreaTriggerScriptId(atEntry->id)];

    if (!pTempScript || !pTempScript->pAreaTrigger)
        return false;

    return pTempScript->pAreaTrigger(pPlayer, atEntry);
}

MANGOS_DLL_EXPORT
bool ProcessEvent(
    uint32 uiEventId, Object* pSource, Object* pTarget, bool bIsStart)
{
    Script* pTempScript = m_scripts[GetEventIdScriptId(uiEventId)];

    if (!pTempScript || !pTempScript->pProcessEventId)
        return false;

    // bIsStart may be false, when event is from taxi node events
    // (arrival=false, departure=true)
    return pTempScript->pProcessEventId(uiEventId, pSource, pTarget, bIsStart);
}

MANGOS_DLL_EXPORT
CreatureAI* GetCreatureAI(Creature* pCreature)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->GetAI)
        return NULL;

    return pTempScript->GetAI(pCreature);
}

MANGOS_DLL_EXPORT
bool ItemUse(Player* pPlayer, Item* pItem, SpellCastTargets const& targets)
{
    Script* pTempScript = m_scripts[pItem->GetProto()->ScriptId];

    if (!pTempScript || !pTempScript->pItemUse)
        return false;

    return pTempScript->pItemUse(pPlayer, pItem, targets);
}

MANGOS_DLL_EXPORT
bool EffectDummyCreature(
    Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, Creature* pTarget)
{
    Script* pTempScript = m_scripts[pTarget->GetScriptId()];

    if (!pTempScript || !pTempScript->pEffectDummyNPC)
        return false;

    return pTempScript->pEffectDummyNPC(pCaster, spellId, effIndex, pTarget);
}

MANGOS_DLL_EXPORT
bool EffectDummyGameObject(Unit* pCaster, uint32 spellId,
    SpellEffectIndex effIndex, GameObject* pTarget)
{
    Script* pTempScript = m_scripts[pTarget->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pEffectDummyGO)
        return false;

    return pTempScript->pEffectDummyGO(pCaster, spellId, effIndex, pTarget);
}

MANGOS_DLL_EXPORT
bool EffectDummyItem(
    Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, Item* pTarget)
{
    Script* pTempScript = m_scripts[pTarget->GetProto()->ScriptId];

    if (!pTempScript || !pTempScript->pEffectDummyItem)
        return false;

    return pTempScript->pEffectDummyItem(pCaster, spellId, effIndex, pTarget);
}

MANGOS_DLL_EXPORT
bool AuraDummy(Aura const* pAura, bool bApply)
{
    Script* pTempScript =
        m_scripts[((Creature*)pAura->GetTarget())->GetScriptId()];

    if (!pTempScript || !pTempScript->pEffectAuraDummy)
        return false;

    return pTempScript->pEffectAuraDummy(pAura, bApply);
}

MANGOS_DLL_EXPORT
InstanceData* CreateInstanceData(Map* pMap)
{
    Script* pTempScript = m_scripts[pMap->GetScriptId()];

    if (!pTempScript || !pTempScript->GetInstanceData)
        return NULL;

    return pTempScript->GetInstanceData(pMap);
}
