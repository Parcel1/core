#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Policies/SingletonImp.h"
#include "Item.h"
#include "AuctionHouseMgr.h"
#include "ObjectMgr.h"
#include "AuctionHouseBotMgr.h"
#include "Config/Config.h"
#include "Chat.h"
#include "World.h"
#include "TransactionLog.h"
#include "Utilities/Random.h"

#include <vector>

INSTANTIATE_SINGLETON_1(AuctionHouseBotMgr);

AuctionHouseBotMgr::~AuctionHouseBotMgr()
{
    m_items.clear();

    if (m_config)
        m_config.reset();
}

void AuctionHouseBotMgr::Load()
{
    /* 1 - DELETE */
    m_items.clear();
    m_loaded = false;

    if (m_config)
        m_config.reset();

    /*2 - LOAD */
    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `item`, `stack`, `bid`, `buyout` FROM `auctionhousebot`"));

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 AuctionHouseBot items");
        return;
    }

    uint32 count = 0;
    BarGoLink bar(result->GetRowCount());

    Field* fields;
    do
    {
        bar.step();
        AuctionHouseBotEntry e;
        fields    = result->Fetch();
        e.item    = fields[0].GetUInt32();
        e.stack   = fields[1].GetUInt32();
        e.bid     = fields[2].GetUInt32();
        e.buyout  = fields[3].GetUInt32();

        m_items.push_back(e);

        ++count;
    }
    while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u AuctionHouseBot items", count);

    /* CONFIG */
    m_config                 = std::make_unique<AuctionHouseBotConfig>();
    m_config->enable         = sConfig.GetBoolDefault("AHBot.Enable", false);
    m_config->ahfid          = sConfig.GetIntDefault("AHBot.ah.fid", 120);
    m_config->itemcount      = sConfig.GetIntDefault("AHBot.itemcount", 2);
    m_config->buyer          = sConfig.GetBoolDefault("AHBot.Buyer.Enable", false);
    m_config->buyerPriceFactor = sConfig.GetFloatDefault("AHBot.Buyer.PriceFactor", 1.0f);
    if (m_config->buyerPriceFactor < 0.0f)
        m_config->buyerPriceFactor = 0.0f;

    m_auctionHouseEntry = sAuctionMgr.GetAuctionHouseEntry(m_config->ahfid);
    if (!m_auctionHouseEntry)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::Load() : No auction house for faction %u.", m_config->ahfid);
        return;
    }
    m_loaded = true;
}

void AuctionHouseBotMgr::Update(bool force /* = false */)
{
    if (!m_loaded)
        return;

    ASSERT(m_config);
    ASSERT(m_auctionHouseEntry);

    if (!(m_config->enable || force))
        return;

    if (m_items.empty())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "AHBot::Update() : Bad config or empty table.");
        return;
    }

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(m_auctionHouseEntry);
    if (!auctionHouse)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "AHBot::Update() : No auction house for faction %u.", m_config->ahfid);
        return;
    }

    uint32 auctions     = auctionHouse->GetCount();
    uint32 items        = m_config->itemcount;
    uint32 entriesCount = m_items.size();

    while (auctions < items)
    {
        AuctionHouseBotEntry item = m_items[urand(0, entriesCount - 1)];
        AddItem(item, auctionHouse);
        auctions++;
    }
}

void AuctionHouseBotMgr::AddItem(AuctionHouseBotEntry e, AuctionHouseObject *auctionHouse)
{
    ASSERT(m_auctionHouseEntry);

    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(e.item);
    if (prototype == nullptr)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::AddItem() : Item %u does not exist.", e.item);
        return;
    }

    Item* item = Item::CreateItem(e.item, 1);
    if (!item)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::AddItem() : Cannot create item.");
        return;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::AddItem() : Adding item %u.", e.item);

    uint32 randomPropertyId = Item::GenerateItemRandomPropertyId(e.item);
    if (randomPropertyId != 0)
        item->SetItemRandomProperties(randomPropertyId);

    uint32 etime = urand(1, 3);
    switch (etime)
    {
        case 1:
            etime = 43200;
            break;
        case 2:
            etime = 86400;
            break;
        case 3:
            etime = 172800;
            break;
        default:
            etime = 86400;
            break;
    }
    item->SetCount(e.stack);

    uint32 dep = sAuctionMgr.GetAuctionDeposit(m_auctionHouseEntry, etime, item);

    AuctionEntry* auctionEntry       = new AuctionEntry;
    auctionEntry->Id                 = sObjectMgr.GenerateAuctionID();
    auctionEntry->auctionHouseEntry  = m_auctionHouseEntry;
    auctionEntry->itemGuidLow        = item->GetGUIDLow();
    auctionEntry->itemTemplate       = item->GetEntry();
    auctionEntry->owner              = 0;
    auctionEntry->startbid           = e.bid;
    auctionEntry->buyout             = e.buyout;
    auctionEntry->bidder             = 0;
    auctionEntry->bid                = 0;
    auctionEntry->deposit            = dep;
    auctionEntry->depositTime        = time(nullptr);
    auctionEntry->expireTime         = (time_t) etime + time(nullptr);

    item->SaveToDB();

    sAuctionMgr.AddAItem(item);
    auctionHouse->AddAuction(auctionEntry);
    auctionEntry->SaveToDB();
}

uint32 AuctionHouseBotMgr::GetReferencePrice(uint32 itemEntry) const
{
    uint32 best = 0;
    for (auto const& e : m_items)
    {
        if (e.item != itemEntry || !e.stack)
            continue;
        uint32 perPiece = e.buyout / e.stack;
        if (perPiece > best)
            best = perPiece;
    }
    return best;
}

bool AuctionHouseBotMgr::TryBuy(AuctionEntry* auction, AuctionHouseObject* auctionHouse)
{
    if (!m_config || !m_config->buyer || !auction || !auctionHouse)
        return false;
    if (!auction->buyout || !auction->owner || auction->bidder)
        return false;

    Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow);
    if (!item)
        return false;

    uint32 reference = GetReferencePrice(auction->itemTemplate);
    if (!reference)
        return false;

    uint64 maxPay = uint64(double(reference) * item->GetCount() * m_config->buyerPriceFactor);
    if (uint64(auction->buyout) > maxPay)
        return false;

    // Same steps as a player buying out the auction, with nobody as the buyer.
    auction->bidder = 0;
    auction->bid = auction->buyout;

    PlayerTransactionData data;
    data.type = "AHBotBuyout";
    data.parts[0].lowGuid = auction->owner;
    data.parts[0].itemsEntries[0] = auction->itemTemplate;
    data.parts[0].itemsCount[0] = item->GetCount();
    data.parts[0].itemsGuid[0] = auction->itemGuidLow;
    data.parts[1].lowGuid = 0;
    data.parts[1].money = auction->bid;
    sWorld.LogTransaction(data);

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::TryBuy() : Bought auction %u (item %u x%u) from player %u for %u.",
             auction->Id, auction->itemTemplate, item->GetCount(), auction->owner, auction->bid);

    sAuctionMgr.SendAuctionSuccessfulMail(auction);     // the seller gets the money by mail, at once

    sAuctionMgr.RemoveAItem(auction->itemGuidLow);
    CharacterDatabase.PExecute("DELETE FROM `item_instance` WHERE `guid` = '%u'", auction->itemGuidLow);
    delete item;

    auctionHouse->RemoveAuction(auction);
    auction->DeleteFromDB();
    delete auction;
    return true;
}

uint32 AuctionHouseBotMgr::RemoveOwnListings()
{
    if (!m_loaded)
        return 0;

    ASSERT(m_auctionHouseEntry);
    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(m_auctionHouseEntry);
    if (!auctionHouse)
        return 0;

    std::vector<AuctionEntry*> ours;
    for (auto const& itr : *auctionHouse->GetAuctions())
    {
        AuctionEntry* auction = itr.second;
        if (auction && auction->owner == 0 && auction->bidder == 0)
            ours.push_back(auction);
    }

    for (AuctionEntry* auction : ours)
    {
        if (Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow))
        {
            sAuctionMgr.RemoveAItem(auction->itemGuidLow);
            CharacterDatabase.PExecute("DELETE FROM `item_instance` WHERE `guid` = '%u'", auction->itemGuidLow);
            delete item;
        }
        auctionHouse->RemoveAuction(auction);
        auction->DeleteFromDB();
        delete auction;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::RemoveOwnListings() : Removed %u listings.", uint32(ours.size()));
    return uint32(ours.size());
}

bool ChatHandler::HandleAHBotUpdateCommand(char *args)
{
    sAuctionHouseBotMgr.Update(true);
    SendSysMessage("[AHBot] Update finished.");
    return true;
}

bool ChatHandler::HandleAHBotRefreshCommand(char *args)
{
    uint32 removed = sAuctionHouseBotMgr.RemoveOwnListings();
    sAuctionHouseBotMgr.Update(true);
    PSendSysMessage("[AHBot] Refresh finished: %u listings replaced with new ones.", removed);
    return true;
}

bool ChatHandler::HandleAHBotReloadCommand(char *args)
{
    sAuctionHouseBotMgr.Load();
    SendSysMessage("[AHBot] Reload finished.");
    return true;
}
