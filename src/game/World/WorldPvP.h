#ifndef WORLD_PVP_H
#define WORLD_PVP_H

#include "Chat/Chat.h"

class Player;
class ChatHandler;

class WorldPvP
{
    public:
        WorldPvP();
        ~WorldPvP();
        void HandlePlayerKill(Player* killer, Player* victim, Group* groupKill);
        void LoadWorldPvPLootMap();
    private:
        bool GetRandomPvPReward(Player* killer);
        int GetRewardQuality();
        int FindReward(int quality, int iLevel);
};

#define sWorldPvP MaNGOS::Singleton<WorldPvP>::Instance()
#endif