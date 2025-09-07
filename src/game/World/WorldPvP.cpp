#include "World/WorldPvP.h"
#include "Entities/Item.h"
#include "Entities/Player.h"
#include "Chat/Chat.h"
#include "Groups/Group.h"
#include <vector>

INSTANTIATE_SINGLETON_1(WorldPvP);

enum QUALITY 
{
    NONE = 1,
    UNCOMMON = 2,
    RARE = 3,
    EPIC = 4
};

typedef struct 
{
    int entry;
    std::string name;
} WorldPvPLootItem;

std::map<int, std::vector<WorldPvPLootItem>> UncommonWorldPvPLootTable;
std::map<int, std::vector<WorldPvPLootItem>> RareWorldPvPLootTable;
std::map<int, std::vector<WorldPvPLootItem>> EpicWorldPvPLootTable;

const static float UNCOMMON_RATIO = 0.4f;
const static float RARE_RATIO = 0.08f;
const static float EPIC_RATIO = 0.03f;
const static float RAID_GROUP_MONEY_REWARD_RATIO = 0.975f;
const static float NON_RAID_GROUP_MONEY_REWARD_RATIO = 0.9f;
const static float PVP_XP_RATIO = 0.25f;
const static float PVP_XP_OFFSET_A = 950.0f;
const static float PVP_XP_OFFSET_B = 1000.0f;

static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

WorldPvP::WorldPvP() { }
WorldPvP::~WorldPvP() { }

void WorldPvP::LoadWorldPvPLootMap() 
{
    for (int quality = UNCOMMON; quality <= EPIC ; quality++) 
    {
        auto queryResult = WorldDatabase.PQuery("select * from worldpvp_loot where quality = '%d'", quality);
        if (!queryResult) continue;

        do 
        {
            Field* fields = queryResult->Fetch();
            if (!fields) 
            {
                sLog.outError("WorldPvP: Invalid fields in database query for quality %d", quality);
                break;
            }
            
            WorldPvPLootItem item = WorldPvPLootItem 
            {
                fields[1].GetInt32(),
                fields[4].GetString()
            };
            int iLevel = fields[2].GetInt32();

            switch (quality) 
            {
                case UNCOMMON:
                    UncommonWorldPvPLootTable[iLevel].push_back(item);
                    break;
                case RARE:
                    RareWorldPvPLootTable[iLevel].push_back(item);
                    break;
                case EPIC:
                    EpicWorldPvPLootTable[iLevel].push_back(item);
                    break;
                default:
                    break;
            }
        } while (queryResult->NextRow());
    }

    size_t totalUncommon = 0, totalRare = 0, totalEpic = 0;
    
    for (const auto& pair : UncommonWorldPvPLootTable) 
    {
        totalUncommon += pair.second.size();
    }
    for (const auto& pair : RareWorldPvPLootTable) 
    {
        totalRare += pair.second.size();
    }
    for (const auto& pair : EpicWorldPvPLootTable) 
    {
        totalEpic += pair.second.size();
    }

    sLog.outString("Loaded %zu Uncommon PvP Loot Items", totalUncommon);
    sLog.outString("Loaded %zu Rare PvP Loot Items", totalRare);
    sLog.outString("Loaded %zu Epic PvP Loot Items", totalEpic);
}

void WorldPvP::HandlePlayerKill(Player* attacker, Player* victim, Group* group) 
{
    if (!attacker || !victim) 
    {
        sLog.outError("WorldPvP::HandlePlayerKill - null pointer: attacker=%p, victim=%p", attacker, victim);
        return;
    }

    GetRandomPvPReward(attacker);
    attacker->GivePlayerKillXP(attacker->GetLevel() * urand(PVP_XP_OFFSET_A * PVP_XP_RATIO, PVP_XP_OFFSET_B * PVP_XP_RATIO), victim);

    if (group) 
    {
        GroupReference* itr = group->GetFirstMember();

        float groupMemberCount = 1.0f;
        while (itr->hasNext()) 
        {
            groupMemberCount++;
        }

        float rewardRatio = 0.f;

        if (group->IsRaidGroup()) 
        {
            rewardRatio = 1.0 - (groupMemberCount / MAX_RAID_SIZE) * RAID_GROUP_MONEY_REWARD_RATIO;
        }
        else 
        {
            rewardRatio = 1.0 - (groupMemberCount / MAX_GROUP_SIZE) * NON_RAID_GROUP_MONEY_REWARD_RATIO;
        }

        attacker->ModifyMoney((float)(victim->GetLevel() * 100) * rewardRatio);
    } 
    else 
    {
        attacker->ModifyMoney(victim->GetLevel() * 100);
    }
}

bool WorldPvP::GetRandomPvPReward(Player* attacker) 
{
    if (!attacker) 
    {
        sLog.outError("WorldPvP::GetRandomPvPReward - attacker is null");
        return false;
    }

    int itemId = 0;
    int quality = GetRewardQuality();

    if (quality == NONE) 
        return false;

    int playerLevel = attacker->GetLevel();
    
    int attempts = 0;
    const int MAX_ATTEMPTS = 100;

    if (playerLevel == 60 && quality > UNCOMMON) 
    {
        while (itemId == 0 && attempts < MAX_ATTEMPTS) 
        {
            int iLevel = urand(60, 92);
            itemId = FindReward(quality, iLevel);
            attempts++;
        }
    } 
    else if (playerLevel < 10) 
    {
        while (itemId == 0 && attempts < MAX_ATTEMPTS) 
        {
            int iLevel = urand(playerLevel, playerLevel + 10);
            itemId = FindReward(quality, iLevel);
            attempts++;
        }
    } 
    else 
    {
        while (itemId == 0 && attempts < MAX_ATTEMPTS) 
        {
            int iLevel = urand(playerLevel, playerLevel + 5);
            itemId = FindReward(quality, iLevel);
            attempts++;
        }
    }

    if (itemId == 0) 
    {
        sLog.outError("WorldPvP::GetRandomPvPReward - Could not find valid item after %d attempts for player level %d, quality %d", MAX_ATTEMPTS, playerLevel, quality);
        return false;
    }

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(itemId);
    if (!pProto) 
    {
        sLog.outError("WorldPvP::GetRandomPvPReward - Invalid item prototype for itemId %d", itemId);
        return false;
    }

    uint32 noSpaceForCount = 0;

    ItemPosCountVec dest;
    uint8 msg = attacker->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, 1, &noSpaceForCount);
    if (msg != EQUIP_ERR_OK) 
    {
        sLog.outDebug("WorldPvP::GetRandomPvPReward - Player %s cannot store item %d (error: %u)", attacker->GetName(), itemId, msg);
        return false;
    }

    Item* item = attacker->StoreNewItem(dest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));
    if (!item) 
    {
        sLog.outError("WorldPvP::GetRandomPvPReward - Failed to create item %d for player %s", itemId, attacker->GetName());
        return false;
    }

    item->SetBinding(false);
    item->SetState(ITEM_CHANGED, attacker);

    attacker->SendNewItem(item, 1, true, false);

    if (noSpaceForCount > 0) 
    {
        sLog.outDebug("WorldPvP::GetRandomPvPReward - No space for %u items", noSpaceForCount);
        return false;
    }

    return true;
}

int WorldPvP::FindReward(int quality, int iLevel) 
{
    switch (quality) 
    {
        case UNCOMMON: 
        {
            auto it = UncommonWorldPvPLootTable.find(iLevel);
            if (it == UncommonWorldPvPLootTable.end() || it->second.empty()) 
                return 0;

            const std::vector<WorldPvPLootItem>& uncommonLootItems = it->second;
            return uncommonLootItems[urand(0, uncommonLootItems.size() - 1)].entry;
        }
        case RARE: 
        {
            auto it = RareWorldPvPLootTable.find(iLevel);
            if (it == RareWorldPvPLootTable.end() || it->second.empty()) 
                return 0;

            const std::vector<WorldPvPLootItem>& rareLootItems = it->second;
            return rareLootItems[urand(0, rareLootItems.size() - 1)].entry;
        }
        case EPIC: 
        {
            auto it = EpicWorldPvPLootTable.find(iLevel);
            if (it == EpicWorldPvPLootTable.end() || it->second.empty()) 
                return 0;

            const std::vector<WorldPvPLootItem>& epicLootItems = it->second;
            return epicLootItems[urand(0, epicLootItems.size() - 1)].entry;
        }
        default:
            sLog.outError("WorldPvP::FindReward - Invalid quality %d", quality);
            return 0;
    }
}

int WorldPvP::GetRewardQuality() 
{
    float randomValue = dis(gen);

    if (randomValue < EPIC_RATIO) 
    {
        return EPIC;
    } 
    else if (randomValue < EPIC_RATIO + RARE_RATIO) 
    {
        return RARE;
    } 
    else if (randomValue < EPIC_RATIO + RARE_RATIO + UNCOMMON_RATIO) 
    {
        return UNCOMMON;
    } 
    else
    {
        return NONE;
    }
}