// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "web_services/web_service.h"
#include "web_client.h"
#include "base/constants.h"
#include "base/clock.h"

namespace mmo
{
	class RealmManager;
	class PlayerManager;
	struct IDatabase;

	class WebService 
		: public web::WebService
	{
	public:

		explicit WebService(
		    asio::io_service &service,
		    uint16 port,
		    String password,
		    PlayerManager &playerManager,
			RealmManager &realmManager,
			IDatabase &database
		);

		PlayerManager &GetPlayerManager() const { return m_playerManager; }
		RealmManager& GetRealmManager() const { return m_realmManager; }
		IDatabase &GetDatabase() const { return m_database; }
		GameTime GetStartTime() const;
		const String &GetPassword() const;

		virtual web::WebService::WebClientPtr createClient(std::shared_ptr<Client> connection) override;

	private:

		PlayerManager &m_playerManager;
		RealmManager& m_realmManager;
		IDatabase &m_database;
		const GameTime m_startTime;
		const String m_password;
	};
}
