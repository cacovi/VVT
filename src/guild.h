/**
 * @file guild.h
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

#ifndef OT_SRC_GUILD_H_
#define OT_SRC_GUILD_H_

class Player;

struct GuildRank {
	uint32_t id;
	std::string name;
	uint8_t level;

	GuildRank(uint32_t initId, std::string initName, uint8_t initLevel) :
		id(initId), name(std::move(initName)), level(initLevel) {}
};

using GuildRank_ptr = std::shared_ptr<GuildRank>;
class Guild
{
	public:
		Guild(uint32_t initId, std::string initName) : name(std::move(initName)), id(initId) {}

		void addMember(Player* player);
		void removeMember(Player* player);

		uint32_t getId() const {
			return id;
		}
		const std::string& getName() const {
			return name;
		}
		const std::list<Player*>& getMembersOnline() const {
			return membersOnline;
		}
		uint32_t getMemberCount() const {
			return memberCount;
		}
		void setMemberCount(uint32_t count) {
			memberCount = count;
		}

		const std::vector<GuildRank_ptr>& getRanks() const {
			return ranks;
		}

		GuildRank_ptr getRankById(uint32_t id);
		GuildRank_ptr getRankByName(const std::string& name) const;
		GuildRank_ptr getRankByLevel(uint8_t level) const;
		void addRank(uint32_t id, const std::string& name, uint8_t level);

		const std::string& getMotd() const {
			return motd;
		}
		void setMotd(const std::string& newMotd) {
			this->motd = newMotd;
		}

		uint32_t getPrivateWarRival() const {
			return privateWarRival;
		}
		
		void setPrivateWarRival(uint32_t rival);

	private:
		std::list<Player*> membersOnline;
		std::vector<GuildRank_ptr> ranks;
		std::string name;
		std::string motd;
		uint32_t id;
		uint32_t memberCount = 0;
		uint32_t privateWarRival = 0;
};

#endif
