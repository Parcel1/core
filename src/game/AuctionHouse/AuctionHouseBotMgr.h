#ifndef _AUCTION_HOUSE_BOT_MGR_H
#define _AUCTION_HOUSE_BOT_MGR_H

#include "Policies/Singleton.h"
#include "SharedDefines.h"

#include <vector>
#include <memory>

class Player;
struct AuctionEntry;
class AuctionHouseObject;

struct AuctionHouseBotEntry
{
    uint32 item;
    uint32 stack;
    uint32 bid;
    uint32 buyout;
};

struct AuctionHouseBotConfig
{
    uint32 itemcount;
    uint32 ahfid;

    bool enable;

    // Buyer: the bot buys a player's auction the moment it is listed, as long as the buyout
    // is not above the price the bot itself sells that item for (times buyerPriceFactor).
    bool buyer;
    float buyerPriceFactor;
};

class AuctionHouseBotMgr
{
    public :
        AuctionHouseBotMgr() = default;
        ~AuctionHouseBotMgr();

        void Load();

        // force - put items in AH even if bot is disabled
        void Update(bool force = false);
        void AddItem(AuctionHouseBotEntry e, AuctionHouseObject *auctionHouse);

        // Called right after a player listed an auction. Returns true when the bot bought it
        // (the auction and its item are gone afterwards; the seller got the money by mail).
        bool TryBuy(AuctionEntry* auction, AuctionHouseObject* auctionHouse);
        // Highest price per piece the bot sells the item for, 0 if it does not sell it.
        uint32 GetReferencePrice(uint32 itemEntry) const;

    protected:
        std::vector<AuctionHouseBotEntry> m_items;
        std::unique_ptr<AuctionHouseBotConfig> m_config;
        AuctionHouseEntry const* m_auctionHouseEntry = nullptr;

        bool m_loaded = false;
};

#define sAuctionHouseBotMgr MaNGOS::Singleton< AuctionHouseBotMgr >::Instance()
#endif
