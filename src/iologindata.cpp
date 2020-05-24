/**
 * @file iologindata.cpp
 * 
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2020 Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include <boost/range/adaptor/reversed.hpp>
#include "iologindata.h"
#include "configmanager.h"
#include "game.h"
#include "scheduler.h"
#include "prey.h"

extern ConfigManager g_config;
extern Game g_game;

Account IOLoginData::loadAccount(uint32_t accno)
{
	Account account;

	std::ostringstream query;
	query << "SELECT `id`, `name`, `password`, `type`, `premdays`, `lastday` FROM `accounts` WHERE `id` = " << accno;
	DBResult_ptr result = Database::getInstance().storeQuery(query.str());
	if (!result) {
		return account;
	}

	account.id = result->getNumber<uint32_t>("id");
	account.name = result->getString("name");
	account.accountType = static_cast<AccountType_t>(result->getNumber<int32_t>("type"));
	account.premiumDays = result->getNumber<uint16_t>("premdays");
	account.lastDay = result->getNumber<time_t>("lastday");
	return account;
}

bool IOLoginData::saveAccount(const Account& acc)
{
	std::ostringstream query;
	query << "UPDATE `accounts` SET `premdays` = " << acc.premiumDays << ", `lastday` = " << acc.lastDay << " WHERE `id` = " << acc.id;
	return Database::getInstance().executeQuery(query.str());
}

std::string decodeSecret(const std::string& secret)
{
	// simple base32 decoding
	std::string key;
	key.reserve(10);

	uint32_t buffer = 0, left = 0;
	for (const auto& ch : secret) {
		buffer <<= 5;
		if (ch >= 'A' && ch <= 'Z') {
			buffer |= (ch & 0x1F) - 1;
		} else if (ch >= '2' && ch <= '7') {
			buffer |= ch - 24;
		} else {
			// if a key is broken, return empty and the comparison
			// will always be false since the token must not be empty
			return {};
		}

		left += 5;
		if (left >= 8) {
			left -= 8;
			key.push_back(static_cast<char>(buffer >> left));
		}
	}

	return key;
}

bool IOLoginData::loginserverAuthentication(const std::string& name, const std::string& password, Account& account)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `id`, `name`, `password`, `secret`, `type`, `premdays`, `lastday` FROM `accounts` WHERE `name` = " << db.escapeString(name);
	DBResult_ptr result = db.storeQuery(query.str());
	if (!result) {
		return false;
	}

	if (transformToSHA1(password) != result->getString("password")) {
		return false;
	}

	account.id = result->getNumber<uint32_t>("id");
	account.name = result->getString("name");
	account.key = decodeSecret(result->getString("secret"));
	account.accountType = static_cast<AccountType_t>(result->getNumber<int32_t>("type"));
	account.premiumDays = result->getNumber<uint16_t>("premdays");
	account.lastDay = result->getNumber<time_t>("lastday");

	query.str(std::string());
	query << "SELECT `name`, `deletion` FROM `players` WHERE `account_id` = " << account.id;
	result = db.storeQuery(query.str());
	if (result) {
		do {
			if (result->getNumber<uint64_t>("deletion") == 0) {
				account.characters.push_back(result->getString("name"));
			}
		} while (result->next());
		std::sort(account.characters.begin(), account.characters.end());
	}
	return true;
}

uint32_t IOLoginData::gameworldAuthentication(const std::string& accountName, const std::string& password, std::string& characterName)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `id`, `password` FROM `accounts` WHERE `name` = " << db.escapeString(accountName);
	DBResult_ptr result = db.storeQuery(query.str());
	if (!result) {
		std::cout << "[IOLoginData::gameworldAuthentication] Account not found!" << std::endl;		
		return 0;
	}

	if (transformToSHA1(password) != result->getString("password")) {
		std::cout << "[IOLoginData::gameworldAuthentication] Wrong Password! " << transformToSHA1(password) << "!=" << result->getString("password") << std::endl;		
		return 0;
	}

	uint32_t accountId = result->getNumber<uint32_t>("id");

	query.str(std::string());
	query << "SELECT `account_id`, `name`, `deletion` FROM `players` WHERE `name` = " << db.escapeString(characterName);
	result = db.storeQuery(query.str());
	if (!result) {
		std::cout << "[IOLoginData::gameworldAuthentication] Not able to find player(" << characterName << ")" << std::endl;		
		return 0;
	}

	if (result->getNumber<uint32_t>("account_id") != accountId || result->getNumber<uint64_t>("deletion") != 0) {
		std::cout << "[IOLoginData::gameworldAuthentication] Account mismatch or account has been marked as deleted!" << std::endl;		
		return 0;
	}
	characterName = result->getString("name");
	return accountId;
}

uint32_t IOLoginData::getAccountIdByPlayerName(const std::string& playerName)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `account_id` FROM `players` WHERE `name` = " << db.escapeString(playerName);
	DBResult_ptr result = db.storeQuery(query.str());
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("account_id");
}

AccountType_t IOLoginData::getAccountType(uint32_t accountId)
{
	std::ostringstream query;
	query << "SELECT `type` FROM `accounts` WHERE `id` = " << accountId;
	DBResult_ptr result = Database::getInstance().storeQuery(query.str());
	if (!result) {
		return ACCOUNT_TYPE_NORMAL;
	}
	return static_cast<AccountType_t>(result->getNumber<uint16_t>("type"));
}

void IOLoginData::setAccountType(uint32_t accountId, AccountType_t accountType)
{
	std::ostringstream query;
	query << "UPDATE `accounts` SET `type` = " << static_cast<uint16_t>(accountType) << " WHERE `id` = " << accountId;
	Database::getInstance().executeQuery(query.str());
}

void IOLoginData::updateOnlineStatus(uint32_t guid, bool login)
{
	if (g_config.getBoolean(ConfigManager::ALLOW_CLONES)) {
		return;
	}

	std::ostringstream query;
	if (login) {
		query << "INSERT INTO `players_online` VALUES (" << guid << ')';
	} else {
		query << "DELETE FROM `players_online` WHERE `player_id` = " << guid;
	}
	Database::getInstance().executeQuery(query.str());
}

bool IOLoginData::preloadPlayer(Player* player, const std::string& name)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `id`, `account_id`, `group_id`, `deletion`, (SELECT `type` FROM `accounts` WHERE `accounts`.`id` = `account_id`) AS `account_type`";
	if (!g_config.getBoolean(ConfigManager::FREE_PREMIUM)) {
		query << ", (SELECT `premdays` FROM `accounts` WHERE `accounts`.`id` = `account_id`) AS `premium_days`";
	}
	query << " FROM `players` WHERE `name` = " << db.escapeString(name);
	DBResult_ptr result = db.storeQuery(query.str());
	if (!result) {
		return false;
	}

	if (result->getNumber<uint64_t>("deletion") != 0) {
		return false;
	}

	player->setGUID(result->getNumber<uint32_t>("id"));
	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));
	if (!group) {
		std::cout << "[Error - IOLoginData::preloadPlayer] " << player->name << " has Group ID " << result->getNumber<uint16_t>("group_id") << " which doesn't exist." << std::endl;
		return false;
	}
	player->setGroup(group);
	player->accountNumber = result->getNumber<uint32_t>("account_id");
	player->accountType = static_cast<AccountType_t>(result->getNumber<uint16_t>("account_type"));
	if (!g_config.getBoolean(ConfigManager::FREE_PREMIUM)) {
		player->premiumDays = result->getNumber<uint16_t>("premium_days");
	} else {
		player->premiumDays = std::numeric_limits<uint16_t>::max();
	}
	return true;
}

bool IOLoginData::loadPlayerById(Player* player, uint32_t id)
{
	Database& db = Database::getInstance();
	std::ostringstream query;
	query << "SELECT `id`, `name`, `account_id`, `group_id`, `sex`, `vocation`, `experience`, `level`, `maglevel`, `health`, `healthmax`, `blessings1`, `blessings2`, `blessings3`, `blessings4`, `blessings5`, `blessings6`, `blessings7`, `blessings8`, `mana`, `manamax`, `manaspent`, `soul`, `lookbody`, `lookfeet`, `lookhead`, `looklegs`, `looktype`, `lookaddons`, `posx`, `posy`, `posz`, `cap`, `lastlogin`, `lastlogout`, `lastip`, `conditions`, `skulltime`, `skull`, `town_id`, `balance`, `offlinetraining_time`, `offlinetraining_skill`, `stamina`, `skill_fist`, `skill_fist_tries`, `skill_club`, `skill_club_tries`, `skill_sword`, `skill_sword_tries`, `skill_axe`, `skill_axe_tries`, `skill_dist`, `skill_dist_tries`, `skill_shielding`, `skill_shielding_tries`, `skill_fishing`, `skill_fishing_tries`, `skill_critical_hit_chance`, `skill_critical_hit_chance_tries`, `skill_critical_hit_damage`, `skill_critical_hit_damage_tries`, `skill_life_leech_chance`, `skill_life_leech_chance_tries`, `skill_life_leech_amount`, `skill_life_leech_amount_tries`, `skill_mana_leech_chance`, `skill_mana_leech_chance_tries`, `skill_mana_leech_amount`,  `skill_mana_leech_amount_tries`, `xpboost_value`, `xpboost_stamina`, `bonusrerollcount`, `instantrewardtokens`, `quickloot_maincontainer` FROM `players` WHERE `id` = " << id;
	return loadPlayer(player, db.storeQuery(query.str()));
}

bool IOLoginData::loadPlayerByName(Player* player, const std::string& name)
{
	Database& db = Database::getInstance();
	std::ostringstream query;
	query << "SELECT `id`, `name`, `account_id`, `group_id`, `sex`, `vocation`, `experience`, `level`, `maglevel`, `health`, `healthmax`, `blessings1`, `blessings2`, `blessings3`, `blessings4`, `blessings5`, `blessings6`, `blessings7`, `blessings8`, `mana`, `manamax`, `manaspent`, `soul`, `lookbody`, `lookfeet`, `lookhead`, `looklegs`, `looktype`, `lookaddons`, `posx`, `posy`, `posz`, `cap`, `lastlogin`, `lastlogout`, `lastip`, `conditions`, `skulltime`, `skull`, `town_id`, `balance`, `offlinetraining_time`, `offlinetraining_skill`, `stamina`, `skill_fist`, `skill_fist_tries`, `skill_club`, `skill_club_tries`, `skill_sword`, `skill_sword_tries`, `skill_axe`, `skill_axe_tries`, `skill_dist`, `skill_dist_tries`, `skill_shielding`, `skill_shielding_tries`, `skill_fishing`, `skill_fishing_tries`, `skill_critical_hit_chance`, `skill_critical_hit_chance_tries`, `skill_critical_hit_damage`, `skill_critical_hit_damage_tries`, `skill_life_leech_chance`, `skill_life_leech_chance_tries`, `skill_life_leech_amount`, `skill_life_leech_amount_tries`, `skill_mana_leech_chance`, `skill_mana_leech_chance_tries`, `skill_mana_leech_amount`, `skill_mana_leech_amount_tries`, `xpboost_stamina`, `xpboost_value`, `instantrewardtokens`, `quickloot_maincontainer` FROM `players` WHERE `name` = " << db.escapeString(name);
	return loadPlayer(player, db.storeQuery(query.str()));
}

bool IOLoginData::loadPlayer(Player* player, DBResult_ptr result)
{
	if (!result) {
		return false;
	}

	Database& db = Database::getInstance();

	uint32_t accno = result->getNumber<uint32_t>("account_id");
	Account acc = loadAccount(accno);

	player->setGUID(result->getNumber<uint32_t>("id"));
	player->name = result->getString("name");
	player->accountNumber = accno;

	player->accountType = acc.accountType;

	if (g_config.getBoolean(ConfigManager::FREE_PREMIUM)) {
		player->premiumDays = std::numeric_limits<uint16_t>::max();
	} else {
		player->premiumDays = acc.premiumDays;
	}

	player->coinBalance = IOAccount::getCoinBalance(player->getAccount());
	
	player->setBonusRerollCount(result->getNumber<int64_t>("bonusrerollcount"));

	player->useMainContainer = result->getNumber<uint16_t>("quickloot_maincontainer") == 1;

	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));
	if (!group) {
		std::cout << "[Error - IOLoginData::loadPlayer] " << player->name << " has Group ID " << result->getNumber<uint16_t>("group_id") << " which doesn't exist" << std::endl;
		return false;
	}
	player->setGroup(group);

	player->setBankBalance(result->getNumber<uint64_t>("balance"));

	player->setSex(static_cast<PlayerSex_t>(result->getNumber<uint16_t>("sex")));
	player->level = std::max<uint32_t>(1, result->getNumber<uint32_t>("level"));

	uint64_t experience = result->getNumber<uint64_t>("experience");

	uint64_t currExpCount = Player::getExpForLevel(player->level);
	uint64_t nextExpCount = Player::getExpForLevel(player->level + 1);
	if (experience < currExpCount || experience > nextExpCount) {
		experience = currExpCount;
	}

	player->experience = experience;

	if (currExpCount < nextExpCount) {
		player->levelPercent = Player::getPercentLevel(player->experience - currExpCount, nextExpCount - currExpCount);
	} else {
		player->levelPercent = 0;
	}

	player->soul = result->getNumber<uint16_t>("soul");
	player->capacity = result->getNumber<uint32_t>("cap") * 100;
	for (int i = 1; i <= 8; i++) {
		std::ostringstream ss;
		ss << "blessings" << i;
		player->addBlessing(i, result->getNumber<uint16_t>(ss.str()));
	}

	unsigned long conditionsSize;
	const char* conditions = result->getStream("conditions", conditionsSize);
	PropStream propStream;
	propStream.init(conditions, conditionsSize);

	Condition* condition = Condition::createCondition(propStream);
	while (condition) {
		if (condition->unserialize(propStream)) {
			player->storedConditionList.push_front(condition);
		} else {
			delete condition;
		}
		condition = Condition::createCondition(propStream);
	}

	if (!player->setVocation(result->getNumber<uint16_t>("vocation"))) {
		std::cout << "[Error - IOLoginData::loadPlayer] " << player->name << " has Vocation ID " << result->getNumber<uint16_t>("vocation") << " which doesn't exist" << std::endl;
		return false;
	}

	player->mana = result->getNumber<uint32_t>("mana");
	player->manaMax = result->getNumber<uint32_t>("manamax");
	player->magLevel = result->getNumber<uint32_t>("maglevel");

	uint64_t nextManaCount = player->vocation->getReqMana(player->magLevel + 1);
	uint64_t manaSpent = result->getNumber<uint64_t>("manaspent");
	if (manaSpent > nextManaCount) {
		manaSpent = 0;
	}

	player->manaSpent = manaSpent;
	player->magLevelPercent = Player::getPercentLevel(player->manaSpent, nextManaCount);

	player->health = result->getNumber<int32_t>("health");
	player->healthMax = result->getNumber<int32_t>("healthmax");

	player->defaultOutfit.lookType = result->getNumber<uint16_t>("looktype");
	player->defaultOutfit.lookHead = result->getNumber<uint16_t>("lookhead");
	player->defaultOutfit.lookBody = result->getNumber<uint16_t>("lookbody");
	player->defaultOutfit.lookLegs = result->getNumber<uint16_t>("looklegs");
	player->defaultOutfit.lookFeet = result->getNumber<uint16_t>("lookfeet");
	player->defaultOutfit.lookAddons = result->getNumber<uint16_t>("lookaddons");
	player->currentOutfit = player->defaultOutfit;
	player->setBonusRerollCount(result->getNumber<int64_t>("bonusrerollcount"));

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		const time_t skullSeconds = result->getNumber<time_t>("skulltime") - time(nullptr);
		if (skullSeconds > 0) {
			//ensure that we round up the number of ticks
			player->skullTicks = (skullSeconds + 2);

			uint16_t skull = result->getNumber<uint16_t>("skull");
			if (skull == SKULL_RED) {
				player->skull = SKULL_RED;
			} else if (skull == SKULL_BLACK) {
				player->skull = SKULL_BLACK;
			}
		}
	}

	player->loginPosition.x = result->getNumber<uint16_t>("posx");
	player->loginPosition.y = result->getNumber<uint16_t>("posy");
	player->loginPosition.z = result->getNumber<uint16_t>("posz");

	player->lastLoginSaved = result->getNumber<time_t>("lastlogin");
	player->lastLogout = result->getNumber<time_t>("lastlogout");

	player->offlineTrainingTime = result->getNumber<int32_t>("offlinetraining_time") * 1000;
	player->offlineTrainingSkill = result->getNumber<int32_t>("offlinetraining_skill");

	Town* town = g_game.map.towns.getTown(result->getNumber<uint32_t>("town_id"));
	if (!town) {
		std::cout << "[Error - IOLoginData::loadPlayer] " << player->name << " has Town ID " << result->getNumber<uint32_t>("town_id") << " which doesn't exist" << std::endl;
		return false;
	}

	player->town = town;

	const Position& loginPos = player->loginPosition;
	if (loginPos.x == 0 && loginPos.y == 0 && loginPos.z == 0) {
		player->loginPosition = player->getTemplePosition();
	}

	player->staminaMinutes = result->getNumber<uint16_t>("stamina");

	player->setStoreXpBoost(result->getNumber<uint16_t>("xpboost_value"));
	player->setExpBoostStamina(result->getNumber<uint16_t>("xpboost_stamina"));
	player->setInstantRewardTokens(result->getNumber<uint64_t>("instantrewardtokens"));

	static const std::string skillNames[] = {"skill_fist", "skill_club", "skill_sword", "skill_axe", "skill_dist", "skill_shielding", "skill_fishing", "skill_critical_hit_chance", "skill_critical_hit_damage", "skill_life_leech_chance", "skill_life_leech_amount", "skill_mana_leech_chance", "skill_mana_leech_amount"};
	static const std::string skillNameTries[] = {"skill_fist_tries", "skill_club_tries", "skill_sword_tries", "skill_axe_tries", "skill_dist_tries", "skill_shielding_tries", "skill_fishing_tries", "skill_critical_hit_chance_tries", "skill_critical_hit_damage_tries", "skill_life_leech_chance_tries", "skill_life_leech_amount_tries", "skill_mana_leech_chance_tries", "skill_mana_leech_amount_tries"};
	static constexpr size_t size = sizeof(skillNames) / sizeof(std::string);
	for (uint8_t i = 0; i < size; ++i) {
		uint16_t skillLevel = result->getNumber<uint16_t>(skillNames[i]);
		uint64_t skillTries = result->getNumber<uint64_t>(skillNameTries[i]);
		uint64_t nextSkillTries = player->vocation->getReqSkillTries(i, skillLevel + 1);
		if (skillTries > nextSkillTries) {
			skillTries = 0;
		}

		player->skills[i].level = skillLevel;
		player->skills[i].tries = skillTries;
		player->skills[i].percent = Player::getPercentLevel(skillTries, nextSkillTries);
	}

	std::ostringstream query;
	query << "SELECT `guild_id`, `rank_id`, `nick` FROM `guild_membership` WHERE `player_id` = " << player->getGUID();
	if ((result = db.storeQuery(query.str()))) {
		uint32_t guildId = result->getNumber<uint32_t>("guild_id");
		uint32_t playerRankId = result->getNumber<uint32_t>("rank_id");
		player->guildNick = result->getString("nick");

		Guild* guild = g_game.getGuild(guildId);
		if (!guild) {
			guild = IOGuild::loadGuild(guildId);
			g_game.addGuild(guild);
		}

		if (guild) {
			player->guild = guild;
			GuildRank_ptr rank = guild->getRankById(playerRankId);
			if (!rank) {
				query.str(std::string());
				query << "SELECT `id`, `name`, `level` FROM `guild_ranks` WHERE `id` = " << playerRankId;

				if ((result = db.storeQuery(query.str()))) {
					guild->addRank(result->getNumber<uint32_t>("id"), result->getString("name"), result->getNumber<uint16_t>("level"));
				}

				rank = guild->getRankById(playerRankId);
				if (!rank) {
					player->guild = nullptr;
				}
			}

			player->guildRank = rank;

			IOGuild::getWarList(guildId, player->guildWarVector);

			query.str(std::string());
			query << "SELECT COUNT(*) AS `members` FROM `guild_membership` WHERE `guild_id` = " << guildId;
			if ((result = db.storeQuery(query.str()))) {
				guild->setMemberCount(result->getNumber<uint32_t>("members"));
			}
		}
	}

	query.str(std::string());
	query << "SELECT `player_id`, `name` FROM `player_spells` WHERE `player_id` = " << player->getGUID();
	if ((result = db.storeQuery(query.str()))) {
		do {
			player->learnedInstantSpellList.emplace_front(result->getString("name"));
		} while (result->next());
	}

	//load inventory items
	ItemMap itemMap;

	query.str(std::string());
	query << "SELECT `player_id`, `time`, `target`, `unavenged` FROM `player_kills` WHERE `player_id` = " << player->getGUID();
	if ((result = db.storeQuery(query.str()))) {
		do {
			time_t killTime = result->getNumber<time_t>("time");
			if ((time(nullptr) - killTime) <= g_config.getNumber(ConfigManager::FRAG_TIME)) {
				player->unjustifiedKills.emplace_back(result->getNumber<uint32_t>("target"), killTime, result->getNumber<bool>("unavenged"));
			}
		} while (result->next());
	}

	query.str(std::string());
	query << "SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_items` WHERE `player_id` = " << player->getGUID() << " ORDER BY `sid` DESC LIMIT 5000";

	std::vector<std::pair<uint8_t, Container*>> openContainersList;

	if ((result = db.storeQuery(query.str()))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;
			int32_t pid = pair.second;

			Container* itemContainer = item->getContainer();
			if (itemContainer) {
				uint8_t cid = item->getIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER);
				if (cid > 0) {
					openContainersList.emplace_back(std::make_pair(cid, itemContainer));
				}

				uint32_t quicklootOffset = itemContainer->getIntAttr(ITEM_ATTRIBUTE_QUICKLOOT);
				for (uint8_t i = LOOT_ARMOR; i <= LOOT_LAST; ++i) {
					if (hasBitSet(1 << i, quicklootOffset)) {
						player->manageQuicklootContainer(static_cast<LootType_t>(i), itemContainer, true);
					}
				}
			}

			if (pid >= 1 && pid <= 11) {
				player->internalAddThing(pid, item);
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}

	std::sort(openContainersList.begin(), openContainersList.end(), [](const std::pair<uint8_t, Container*> &left, const std::pair<uint8_t, Container*> &right) {
		return left.first < right.first;
	});

	for (auto& it : openContainersList) {
		player->addContainer(it.first - 1, it.second);
		g_scheduler.addEvent(createSchedulerTask(((it.first) * 50), std::bind(&Game::playerUpdateContainer, &g_game, player->getGUID(), it.first - 1)));
	}

	// Store Inbox
	if (!player->inventory[CONST_SLOT_STORE_INBOX]) {
		player->internalAddThing(CONST_SLOT_STORE_INBOX, Item::CreateItem(ITEM_STORE_INBOX));
	}

	//load depot items
	itemMap.clear();

	query.str(std::string());
	query << "SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_depotitems` WHERE `player_id` = " << player->getGUID() << " ORDER BY `sid` DESC LIMIT 4000";
	if ((result = db.storeQuery(query.str()))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;

			int32_t pid = pair.second;
			if (pid >= 0 && pid < 100) {
				DepotChest* depotChest = player->getDepotChest(pid, true);
				if (depotChest) {
					depotChest->internalAddThing(item);
					item->startDecaying();
				}
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
					item->startDecaying();
				}
			}
		}
	}

	//load reward chest items
	itemMap.clear();

	query.str(std::string());
	query << "SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_rewards` WHERE `player_id` = " << player->getGUID() << " ORDER BY `sid` DESC LIMIT 5000";
    if ((result = db.storeQuery(query.str()))) {
		loadItems(itemMap, result);

		//first loop handles the reward containers to retrieve its date attribute
		//for (ItemMap::iterator it = itemMap.begin(), end = itemMap.end(); it != end; ++it) {
		for (auto& it : itemMap) {
			const std::pair<Item*, int32_t>& pair = it.second;
			Item* item = pair.first;

			int32_t pid = pair.second;
			if (pid >= 0 && pid < 100) {
				Reward* reward = player->getReward(item->getIntAttr(ITEM_ATTRIBUTE_DATE), true);
				if (reward) {
					it.second = std::pair<Item*, int32_t>(reward->getItem(), pid); //update the map with the special reward container
				}
			} else {
				break;
			}
		}

		//second loop (this time a reverse one) to insert the items in the correct order
		//for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
		for (const auto& it : boost::adaptors::reverse(itemMap)) {
			const std::pair<Item*, int32_t>& pair = it.second;
			Item* item = pair.first;

			int32_t pid = pair.second;
			if (pid >= 0 && pid < 100) {
				break;
			}

			ItemMap::const_iterator it2 = itemMap.find(pid);
			
			for (const auto& it3 : player->depotLockerMap) {
				it3.second->setParent(VirtualCylinder::virtualCylinder);
				it3.second->startDecaying();
			}
			
			if (it2 == itemMap.end()) {
				continue;
			}

			Container* container = it2->second.first->getContainer();
			if (container) {
				container->internalAddThing(item);
			}
		}
	}

	//load inbox items
	itemMap.clear();

	query.str(std::string());
	query << "SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_inboxitems` WHERE `player_id` = " << player->getGUID() << " ORDER BY `sid` DESC LIMIT 2000";
	if ((result = db.storeQuery(query.str()))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;
			int32_t pid = pair.second;

			if (pid >= 0 && pid < 100) {
				player->getInbox()->internalAddThing(item);
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);

				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}

	//load storage map
	query.str(std::string());
	query << "SELECT `key`, `value` FROM `player_storage` WHERE `player_id` = " << player->getGUID();
	if ((result = db.storeQuery(query.str()))) {
		do {
			player->addStorageValue(result->getNumber<uint32_t>("key"), result->getNumber<int32_t>("value"), true);
		} while (result->next());
	}

	//load vip
	query.str(std::string());
	query << "SELECT `player_id` FROM `account_viplist` WHERE `account_id` = " << player->getAccount();
	if ((result = db.storeQuery(query.str()))) {
		do {
			player->addVIPInternal(result->getNumber<uint32_t>("player_id"));
		} while (result->next());
	}

	//load preydata
	query.str(std::string());
	query << "SELECT `data` FROM `player_preydata` WHERE `player_id` = " << player->getGUID();
	if ((result = db.storeQuery(query.str()))) {
		std::vector<PreyData> preyData(3);
		loadPreyData(preyData, result);
		player->setPreyData(std::move(preyData));
	}
	else {
		player->generatePreyData();
	}

	//send resource
	player->sendResourceData(RESOURCETYPE_BANK_GOLD, player->getBankBalance());
	player->sendResourceData(RESOURCETYPE_INVENTORY_GOLD, player->getMoney());
	player->sendResourceData(RESOURCETYPE_PREY_BONUS_REROLLS, player->getBonusRerollCount());

	player->updateBaseSpeed();
	player->updateInventoryWeight();
	player->updateItemsLight(true);
	return true;
}

void IOLoginData::readPreyList(std::vector<std::string>& preyList, PropStream& propStream)
{
	uint8_t preyListSize;
	if (propStream.read<uint8_t>(preyListSize) && preyListSize > 0) {
		for (uint8_t i = 0; i < preyListSize; i++) {
			std::string preyName;
			if (propStream.readString(preyName)) {
				preyList.emplace_back(std::move(preyName));
			}
		}
	}
}

void IOLoginData::loadPreyData(std::vector<PreyData>& preyData, DBResult_ptr result)
{
	/*
	{
		PREYSLOTID
			LASTFREEREROLLTIME
			STATEID
				-> (0x00 locked)     : PREYLIST SIZE: 0x00
				-> (0x01 inactive)   : PREYLIST SIZE: 0x00
				-> (0x02 active)     : STRING PREYNAME, TIMELEFT, BONUSTYPE, BONUSVALUE, BONUSGRADE, PREYLIST SIZE: Variable, PREYLIST
				-> (0x03 selection)  : PREYLIST SIZE: Variable, PREYLIST
				-> (0X04 selectionc) : BONUSTYPE, BONUSVALUE, BONUSGRADE, PREYLIST SIZE: Variable, PREYLIST
	} x 3
	*/

	unsigned long dataSize;
	const char* data = result->getStream("data", dataSize);

	PropStream propStream;
	propStream.init(data, dataSize);

	PreyState stateId = STATE_LOCKED;
	uint64_t lastReroll = 0;
	uint8_t preySlotId;

	for (int blocks = 0; blocks < PREY_SLOTCOUNT; blocks++) {
		if (propStream.read<uint8_t>(preySlotId) && preySlotId < PREY_SLOTCOUNT) {
			PreyData& currentPrey = preyData[preySlotId];

			if (!propStream.read<uint64_t>(lastReroll)) {
				continue;
			}

			if (propStream.read<PreyState>(stateId)) {

				std::string preyMonster;
				uint16_t timeLeft = 0;
				BonusType bonusType = BONUS_NONE;
				uint16_t bonusValue = 0;
				uint8_t bonusGrade = 0;

				if (stateId == STATE_ACTIVE) {
					if (!propStream.readString(preyMonster)) {
						continue;
					}
					else if (!propStream.read<uint16_t>(timeLeft)) {
						continue;
					}
					else if (!propStream.read<BonusType>(bonusType)) {
						continue;
					}
					else if (!propStream.read<uint16_t>(bonusValue)) {
						continue;
					}
					else if (!propStream.read<uint8_t>(bonusGrade)) {
						continue;
					}
				}
				else if (stateId == STATE_SELECTION_CHANGE_MONSTER) {
					if (!propStream.read<BonusType>(bonusType)) {
						continue;
					}
					else if (!propStream.read<uint16_t>(bonusValue)) {
						continue;
					}
					else if (!propStream.read<uint8_t>(bonusGrade)) {
						continue;
					}
				}

				currentPrey.state = stateId;
				currentPrey.preyMonster = preyMonster;
				currentPrey.timeLeft = timeLeft;
				currentPrey.bonusType = bonusType;
				currentPrey.bonusGrade = bonusGrade;
				currentPrey.bonusValue = bonusValue;
				currentPrey.lastReroll = lastReroll;
				readPreyList(currentPrey.preyList, propStream);
			}
		}
	}
}

bool IOLoginData::savePreyData(const Player* player)
{
	Database& db = Database::getInstance();
	PropWriteStream propWriteStream;
	player->serializePreyData(propWriteStream);

	size_t dataSize;
	const char* data = propWriteStream.getStream(dataSize);

	std::ostringstream ss;
	DBInsert preyQuery("INSERT INTO `player_preydata` (`player_id`, `data`) VALUES ");

	ss << player->getGUID() << ',' << db.escapeBlob(data, dataSize);
	preyQuery.addRow(ss);
	return preyQuery.execute();
}

bool IOLoginData::saveItems(const Player* player, const ItemBlockList& itemList, DBInsert& query_insert, PropWriteStream& propWriteStream)
{
	std::ostringstream ss;

	using ContainerBlock = std::pair<Container*, int32_t>;
	std::list<ContainerBlock> queue;

	int32_t runningId = 100;

	Database& db = Database::getInstance();
	const auto& openContainers = player->getOpenContainers();

	for (const auto& it : itemList) {
		int32_t pid = it.first;
		Item* item = it.second;
		++runningId;

		if (Container* container = item->getContainer()) {
			if (container->getIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER) > 0) {
				container->setIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER, 0);
			}

			if (!openContainers.empty()) {
				for (const auto& its : openContainers) {
					auto openContainer = its.second;
					auto opcontainer = openContainer.container;

					if (opcontainer == container) {
						container->setIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER, ((int)its.first) + 1);
						break;
					}
				}
			}

			queue.emplace_back(container, runningId);
		}

		propWriteStream.clear();
		item->serializeAttr(propWriteStream);

		size_t attributesSize;
		const char* attributes = propWriteStream.getStream(attributesSize);

		ss << player->getGUID() << ',' << pid << ',' << runningId << ',' << item->getID() << ',' << item->getSubType() << ',' << db.escapeBlob(attributes, attributesSize);
		if (!query_insert.addRow(ss)) {
			return false;
		}

	}

	while (!queue.empty()) {
		const ContainerBlock& cb = queue.front();
		Container* container = cb.first;
		int32_t parentId = cb.second;
		queue.pop_front();

		for (Item* item : container->getItemList()) {
			++runningId;

			Container* subContainer = item->getContainer();

			if (subContainer) {
				queue.emplace_back(subContainer, runningId);
				if (subContainer->getIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER) > 0) {
					subContainer->setIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER, 0);
				}

				if (!openContainers.empty()) {
					for (const auto& it : openContainers) {
						auto openContainer = it.second;
						auto opcontainer = openContainer.container;

						if (opcontainer == subContainer) {
							subContainer->setIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER, ((int)it.first) + 1);
							break;
						}
					}
				}
			}

			propWriteStream.clear();
			item->serializeAttr(propWriteStream);

			size_t attributesSize;
			const char* attributes = propWriteStream.getStream(attributesSize);

			ss << player->getGUID() << ',' << parentId << ',' << runningId << ',' << item->getID() << ',' << item->getSubType() << ',' << db.escapeBlob(attributes, attributesSize);
			if (!query_insert.addRow(ss)) {
				return false;
			}
		}
	}
	return query_insert.execute();
}

bool IOLoginData::savePlayer(Player* player)
{
	if (player->getHealth() <= 0) {
		player->changeHealth(1);
	}

	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `save` FROM `players` WHERE `id` = " << player->getGUID();
	DBResult_ptr result = db.storeQuery(query.str());
	if (!result) {
		return false;
	}

	if (result->getNumber<uint16_t>("save") == 0) {
		query.str(std::string());
		query << "UPDATE `players` SET `lastlogin` = " << player->lastLoginSaved << ", `lastip` = " << player->lastIP << " WHERE `id` = " << player->getGUID();
		return db.executeQuery(query.str());
	}

	//serialize conditions
	PropWriteStream propWriteStream;
	for (Condition* condition : player->conditions) {
		if (condition->isPersistent()) {
			condition->serialize(propWriteStream);
			propWriteStream.write<uint8_t>(CONDITIONATTR_END);
		}
	}

	size_t conditionsSize;
	const char* conditions = propWriteStream.getStream(conditionsSize);

	//First, an UPDATE query to write the player itself
	query.str(std::string());
	query << "UPDATE `players` SET ";
	query << "`level` = " << player->level << ',';
	query << "`group_id` = " << player->group->id << ',';
	query << "`vocation` = " << player->getVocationId() << ',';
	query << "`health` = " << player->health << ',';
	query << "`healthmax` = " << player->healthMax << ',';
	query << "`experience` = " << player->experience << ',';
	query << "`lookbody` = " << static_cast<uint32_t>(player->defaultOutfit.lookBody) << ',';
	query << "`lookfeet` = " << static_cast<uint32_t>(player->defaultOutfit.lookFeet) << ',';
	query << "`lookhead` = " << static_cast<uint32_t>(player->defaultOutfit.lookHead) << ',';
	query << "`looklegs` = " << static_cast<uint32_t>(player->defaultOutfit.lookLegs) << ',';
	query << "`looktype` = " << player->defaultOutfit.lookType << ',';
	query << "`lookaddons` = " << static_cast<uint32_t>(player->defaultOutfit.lookAddons) << ',';
	query << "`maglevel` = " << player->magLevel << ',';
	query << "`mana` = " << player->mana << ',';
	query << "`manamax` = " << player->manaMax << ',';
	query << "`manaspent` = " << player->manaSpent << ',';
	query << "`soul` = " << static_cast<uint16_t>(player->soul) << ',';
	query << "`town_id` = " << player->town->getID() << ',';

	const Position& loginPosition = player->getLoginPosition();
	query << "`posx` = " << loginPosition.getX() << ',';
	query << "`posy` = " << loginPosition.getY() << ',';
	query << "`posz` = " << loginPosition.getZ() << ',';

	query << "`cap` = " << (player->capacity / 100) << ',';
	query << "`sex` = " << static_cast<uint16_t>(player->sex) << ',';

	if (player->lastLoginSaved != 0) {
		query << "`lastlogin` = " << player->lastLoginSaved << ',';
	}

	if (player->lastIP != 0) {
		query << "`lastip` = " << player->lastIP << ',';
	}

	query << "`conditions` = " << db.escapeBlob(conditions, conditionsSize) << ',';

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		int64_t skullTime = 0;

		if (player->skullTicks > 0) {
			skullTime = time(nullptr) + player->skullTicks;
		}

		query << "`skulltime` = " << skullTime << ',';

		Skulls_t skull = SKULL_NONE;
		if (player->skull == SKULL_RED) {
			skull = SKULL_RED;
		} else if (player->skull == SKULL_BLACK) {
			skull = SKULL_BLACK;
		}
		query << "`skull` = " << static_cast<int64_t>(skull) << ',';
	}

	query << "`lastlogout` = " << player->getLastLogout() << ',';
	query << "`balance` = " << player->bankBalance << ',';
	query << "`offlinetraining_time` = " << player->getOfflineTrainingTime() / 1000 << ',';
	query << "`offlinetraining_skill` = " << player->getOfflineTrainingSkill() << ',';
	query << "`stamina` = " << player->getStaminaMinutes() << ',';

	query << "`skill_fist` = " << player->skills[SKILL_FIST].level << ',';
	query << "`skill_fist_tries` = " << player->skills[SKILL_FIST].tries << ',';
	query << "`skill_club` = " << player->skills[SKILL_CLUB].level << ',';
	query << "`skill_club_tries` = " << player->skills[SKILL_CLUB].tries << ',';
	query << "`skill_sword` = " << player->skills[SKILL_SWORD].level << ',';
	query << "`skill_sword_tries` = " << player->skills[SKILL_SWORD].tries << ',';
	query << "`skill_axe` = " << player->skills[SKILL_AXE].level << ',';
	query << "`skill_axe_tries` = " << player->skills[SKILL_AXE].tries << ',';
	query << "`skill_dist` = " << player->skills[SKILL_DISTANCE].level << ',';
	query << "`skill_dist_tries` = " << player->skills[SKILL_DISTANCE].tries << ',';
	query << "`skill_shielding` = " << player->skills[SKILL_SHIELD].level << ',';
	query << "`skill_shielding_tries` = " << player->skills[SKILL_SHIELD].tries << ',';
	query << "`skill_fishing` = " << player->skills[SKILL_FISHING].level << ',';
	query << "`skill_fishing_tries` = " << player->skills[SKILL_FISHING].tries << ',';
	query << "`skill_critical_hit_chance` = " << player->skills[SKILL_CRITICAL_HIT_CHANCE].level << ',';
	query << "`skill_critical_hit_chance_tries` = " << player->skills[SKILL_CRITICAL_HIT_CHANCE].tries << ',';
	query << "`skill_critical_hit_damage` = " << player->skills[SKILL_CRITICAL_HIT_DAMAGE].level << ',';
	query << "`skill_critical_hit_damage_tries` = " << player->skills[SKILL_CRITICAL_HIT_DAMAGE].tries << ',';
	query << "`skill_life_leech_chance` = " << player->skills[SKILL_LIFE_LEECH_CHANCE].level << ',';
	query << "`skill_life_leech_chance_tries` = " << player->skills[SKILL_LIFE_LEECH_CHANCE].tries << ',';
	query << "`skill_life_leech_amount` = " << player->skills[SKILL_LIFE_LEECH_AMOUNT].level << ',';
	query << "`skill_life_leech_amount_tries` = " << player->skills[SKILL_LIFE_LEECH_AMOUNT].tries << ',';
	query << "`skill_mana_leech_chance` = " << player->skills[SKILL_MANA_LEECH_CHANCE].level << ',';
	query << "`skill_mana_leech_chance_tries` = " << player->skills[SKILL_MANA_LEECH_CHANCE].tries << ',';
	query << "`skill_mana_leech_amount` = " << player->skills[SKILL_MANA_LEECH_AMOUNT].level << ',';
	query << "`skill_mana_leech_amount_tries` = " << player->skills[SKILL_MANA_LEECH_AMOUNT].tries << ',';
	query << "`xpboost_value` = " << player->getStoreXpBoost() << ',';
	query << "`xpboost_stamina` = " << player->getExpBoostStamina() << ',';
	query << "`bonusrerollcount` = " << player->getBonusRerollCount() << ',';
	query << "`instantrewardtokens` = " << player->getInstantRewardTokens() << ',';
	query << "`quickloot_maincontainer` = " << static_cast<uint16_t>(player->useMainContainer) << ',';

	if (!player->isOffline()) {
		query << "`onlinetime` = `onlinetime` + " << (time(nullptr) - player->lastLoginSaved) << ',';
	}
	for (int i = 1; i <= 8; i++) {
		query << "`blessings" << i << "`" << " = " << static_cast<uint32_t>(player->getBlessingCount(i)) << ((i == 8) ? ' ' : ',');
	}
	query << " WHERE `id` = " << player->getGUID();

	DBTransaction transaction;
	if (!transaction.begin()) {
		return false;
	}

	if (!db.executeQuery(query.str())) {
		return false;
	}

	// learned spells
	query.str(std::string());
	query << "DELETE FROM `player_spells` WHERE `player_id` = " << player->getGUID();
	if (!db.executeQuery(query.str())) {
		return false;
	}

	query.str(std::string());

	DBInsert spellsQuery("INSERT INTO `player_spells` (`player_id`, `name` ) VALUES ");
	for (const std::string& spellName : player->learnedInstantSpellList) {
		query << player->getGUID() << ',' << db.escapeString(spellName);
		if (!spellsQuery.addRow(query)) {
			return false;
		}
	}

	if (!spellsQuery.execute()) {
		return false;
	}

	//player kills
	query.str(std::string());
	query << "DELETE FROM `player_kills` WHERE `player_id` = " << player->getGUID();
	if (!db.executeQuery(query.str())) {
		return false;
	}

	query.str(std::string());

	DBInsert killsQuery("INSERT INTO `player_kills` (`player_id`, `target`, `time`, `unavenged`) VALUES");
	for (const auto& kill : player->unjustifiedKills) {
		query << player->getGUID() << ',' << kill.target << ',' << kill.time << ',' << kill.unavenged;
		if (!killsQuery.addRow(query)) {
			return false;
		}
	}

	if (!killsQuery.execute()) {
		return false;
	}

	//item saving
	query << "DELETE FROM `player_items` WHERE `player_id` = " << player->getGUID();
	if (!db.executeQuery(query.str())) {
		return false;
	}

	DBInsert itemsQuery("INSERT INTO `player_items` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");

	ItemBlockList itemList;
	for (int32_t slotId = 1; slotId <= 11; ++slotId) {
		Item* item = player->inventory[slotId];
		if (item) {
			itemList.emplace_back(slotId, item);
		}
	}

	if (!saveItems(player, itemList, itemsQuery, propWriteStream)) {
		return false;
	}

	if (player->lastDepotId != -1) {
		//save depot items
		query.str(std::string());
		query << "DELETE FROM `player_depotitems` WHERE `player_id` = " << player->getGUID();

		if (!db.executeQuery(query.str())) {
			return false;
		}

		DBInsert depotQuery("INSERT INTO `player_depotitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
		itemList.clear();

		for (const auto& it : player->depotChests) {
			DepotChest* depotChest = it.second;
			for (Item* item : depotChest->getItemList()) {
				itemList.emplace_back(it.first, item);
			}
		}

		if (!saveItems(player, itemList, depotQuery, propWriteStream)) {
			return false;
		}
	}

	//save reward items
	query.str(std::string());
	query << "DELETE FROM `player_rewards` WHERE `player_id` = " << player->getGUID();

	if (!db.executeQuery(query.str())) {
		return false;
	}

	std::vector<uint32_t> rewardList;
	player->getRewardList(rewardList);

	if (!rewardList.empty()) {
		DBInsert rewardQuery("INSERT INTO `player_rewards` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
		itemList.clear();
	
		int running = 0;
		for (const auto& rewardId : rewardList) {
			Reward* reward = player->getReward(rewardId, false);
			// rewards that are empty or older than 7 days aren't stored
			if (!reward->empty() && (time(nullptr) - rewardId <= 60 * 60 * 24 * 7)) {
				itemList.emplace_back(++running, reward);
			}
		}

		if (!saveItems(player, itemList, rewardQuery, propWriteStream)) {
			return false;
		}
	}

	//save inbox items
	query.str(std::string());
	query << "DELETE FROM `player_inboxitems` WHERE `player_id` = " << player->getGUID();
	if (!db.executeQuery(query.str())) {
		return false;
	}

	DBInsert inboxQuery("INSERT INTO `player_inboxitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
	itemList.clear();

	for (Item* item : player->getInbox()->getItemList()) {
		itemList.emplace_back(0, item);
	}

	if (!saveItems(player, itemList, inboxQuery, propWriteStream)) {
		return false;
	}
	
	query.str(std::string());
	query << "DELETE FROM `player_storage` WHERE `player_id` = " << player->getGUID();
	if (!db.executeQuery(query.str())) {
		return false;
	}

	query.str(std::string());

	DBInsert storageQuery("INSERT INTO `player_storage` (`player_id`, `key`, `value`) VALUES ");
	player->genReservedStorageRange();

	for (const auto& it : player->storageMap) {
		query << player->getGUID() << ',' << it.first << ',' << it.second;
		if (!storageQuery.addRow(query)) {
			return false;
		}
	}

	if (!storageQuery.execute()) {
		return false;
	}

	query.str(std::string());
	query << "DELETE FROM `player_preydata` WHERE `player_id` = " << player->getGUID();
	if (!db.executeQuery(query.str())) {
		return false;
	}

	if (!savePreyData(player)) {
		return false;
	}

	//End the transaction
	return transaction.commit();
}

std::string IOLoginData::getNameByGuid(uint32_t guid)
{
	std::ostringstream query;
	query << "SELECT `name` FROM `players` WHERE `id` = " << guid;
	DBResult_ptr result = Database::getInstance().storeQuery(query.str());
	if (!result) {
		return std::string();
	}
	return result->getString("name");
}

uint32_t IOLoginData::getGuidByName(const std::string& name)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `id` FROM `players` WHERE `name` = " << db.escapeString(name);
	DBResult_ptr result = db.storeQuery(query.str());
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("id");
}

bool IOLoginData::getGuidByNameEx(uint32_t& guid, bool& specialVip, std::string& name)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `name`, `id`, `group_id`, `account_id` FROM `players` WHERE `name` = " << db.escapeString(name);
	DBResult_ptr result = db.storeQuery(query.str());
	if (!result) {
		return false;
	}

	name = result->getString("name");
	guid = result->getNumber<uint32_t>("id");
	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));

	uint64_t flags;
	if (group) {
		flags = group->flags;
	} else {
		flags = 0;
	}

	specialVip = (flags & PlayerFlag_SpecialVIP) != 0;
	return true;
}

bool IOLoginData::formatPlayerName(std::string& name)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `name` FROM `players` WHERE `name` = " << db.escapeString(name);

	DBResult_ptr result = db.storeQuery(query.str());
	if (!result) {
		return false;
	}

	name = result->getString("name");
	return true;
}

void IOLoginData::loadItems(ItemMap& itemMap, DBResult_ptr result)
{
	do {
		uint32_t sid = result->getNumber<uint32_t>("sid");
		uint32_t pid = result->getNumber<uint32_t>("pid");
		uint16_t type = result->getNumber<uint16_t>("itemtype");
		uint16_t count = result->getNumber<uint16_t>("count");

		unsigned long attrSize;
		const char* attr = result->getStream("attributes", attrSize);

		PropStream propStream;
		propStream.init(attr, attrSize);

		Item* item = Item::CreateItem(type, count);
		if (item) {
			if (!item->unserializeAttr(propStream)) {
				std::cout << "WARNING: Serialize error in IOLoginData::loadItems" << std::endl;
			}

			std::pair<Item*, uint32_t> pair(item, pid);
			itemMap[sid] = pair;
		}
	} while (result->next());
}

void IOLoginData::increaseBankBalance(uint32_t guid, uint64_t bankBalance)
{
	std::ostringstream query;
	query << "UPDATE `players` SET `balance` = `balance` + " << bankBalance << " WHERE `id` = " << guid;
	Database::getInstance().executeQuery(query.str());
}

bool IOLoginData::hasBiddedOnHouse(uint32_t guid)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "SELECT `id` FROM `houses` WHERE `highest_bidder` = " << guid << " LIMIT 1";
	return db.storeQuery(query.str()).get() != nullptr;
}

std::forward_list<VIPEntry> IOLoginData::getVIPEntries(uint32_t accountId)
{
	std::forward_list<VIPEntry> entries;

	std::ostringstream query;
	query << "SELECT `player_id`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `name`, `description`, `icon`, `notify` FROM `account_viplist` WHERE `account_id` = " << accountId;

	DBResult_ptr result = Database::getInstance().storeQuery(query.str());
	if (result) {
		do {
			entries.emplace_front(
				result->getNumber<uint32_t>("player_id"),
				result->getString("name"),
				result->getString("description"),
				result->getNumber<uint32_t>("icon"),
				result->getNumber<uint16_t>("notify") != 0
			);
		} while (result->next());
	}
	return entries;
}

void IOLoginData::addVIPEntry(uint32_t accountId, uint32_t guid, const std::string& description, uint32_t icon, bool notify)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "INSERT INTO `account_viplist` (`account_id`, `player_id`, `description`, `icon`, `notify`) VALUES (" << accountId << ',' << guid << ',' << db.escapeString(description) << ',' << icon << ',' << notify << ')';
	db.executeQuery(query.str());
}

void IOLoginData::editVIPEntry(uint32_t accountId, uint32_t guid, const std::string& description, uint32_t icon, bool notify)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	query << "UPDATE `account_viplist` SET `description` = " << db.escapeString(description) << ", `icon` = " << icon << ", `notify` = " << notify << " WHERE `account_id` = " << accountId << " AND `player_id` = " << guid;
	db.executeQuery(query.str());
}

void IOLoginData::removeVIPEntry(uint32_t accountId, uint32_t guid)
{
	std::ostringstream query;
	query << "DELETE FROM `account_viplist` WHERE `account_id` = " << accountId << " AND `player_id` = " << guid;
	Database::getInstance().executeQuery(query.str());
}

void IOLoginData::addPremiumDays(uint32_t accountId, int32_t addDays)
{
	std::ostringstream query;
	query << "UPDATE `accounts` SET `premdays` = `premdays` + " << addDays << " WHERE `id` = " << accountId;
	Database::getInstance().executeQuery(query.str());
}

void IOLoginData::removePremiumDays(uint32_t accountId, int32_t removeDays)
{
	std::ostringstream query;
	query << "UPDATE `accounts` SET `premdays` = `premdays` - " << removeDays << " WHERE `id` = " << accountId;
	Database::getInstance().executeQuery(query.str());
}
