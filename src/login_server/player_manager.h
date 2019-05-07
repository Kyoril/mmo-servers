// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include <memory>
#include <mutex>
#include <list>

namespace mmo
{
	class Player;

	/// Manages all connected players.
	class PlayerManager final : public NonCopyable
	{
	public:

		typedef std::list<std::shared_ptr<Player>> Players;

	public:

		/// Initializes a new instance of the player manager class.
		/// @param playerCapacity The maximum number of connections that can be connected at the same time.
		explicit PlayerManager(
		    size_t playerCapacity
		);
		~PlayerManager();

		/// Notifies the manager that a player has been disconnected which will
		/// delete the player instance.
		void playerDisconnected(Player &player);
		/// Determines whether the player capacity limit has been reached.
		bool hasPlayerCapacityBeenReached();
		/// Adds a new player instance to the manager.
		void addPlayer(std::shared_ptr<Player> added);
		/// Gets a player by his account name.
		Player *getPlayerByAccountName(const String &accountName);
		/// 
		Player *getPlayerByAccountID(uint32 accountId);

	private:

		Players m_players;
		size_t m_playerCapacity;
		std::mutex m_playerMutex;
	};
}
