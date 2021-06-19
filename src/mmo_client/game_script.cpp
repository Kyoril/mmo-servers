// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "game_script.h"
#include "console.h"
#include "login_connector.h"
#include "realm_connector.h"
#include "game_state_mgr.h"
#include "world_state.h"
#include "login_state.h"


#include <string>
#include <functional>
#include <utility>


#include "luabind/luabind.hpp"
#include "luabind/iterator_policy.hpp"


namespace mmo
{
	// Static script methods
	namespace
	{
		/// Allows executing a console command from within lua.
		void Script_RunConsoleCommand(const char* cmdLine)
		{
			ASSERT(cmdLine);
			Console::ExecuteCommand(cmdLine);
		}
		
		const mmo::RealmData* Script_GetRealmData(LoginConnector& connector, int32 index)
		{
			const auto& realms = connector.GetRealms();
			if (index < 0 || index >= realms.size())
			{
				ELOG("GetRealm: Invalid realm index provided (" << index << ")");
				return nullptr;
			}

			return &realms[index];
		}

		void Script_EnterWorld()
		{
			GameStateMgr::Get().SetGameState(WorldState::Name);
		}
		
		void Script_Print(const std::string& text)
		{
			ILOG(text);
		}
	}


	GameScript::GameScript(LoginConnector& loginConnector, RealmConnector& realmConnector, std::shared_ptr<LoginState> loginState)
		: m_loginConnector(loginConnector)
		, m_realmConnector(realmConnector)
		, m_loginState(std::move(loginState))
	{
		// Initialize the lua state instance
		m_luaState = LuaStatePtr(luaL_newstate());

		luaL_openlibs(m_luaState.get());
		
		// Initialize luabind
		luabind::open(m_luaState.get());

		// Register custom global functions to the lua state instance
		RegisterGlobalFunctions();
	}

	void GameScript::RegisterGlobalFunctions()
	{
		// Check for double initialization
		ASSERT(!m_globalFunctionsRegistered);

		// Register common functions
		luabind::module(m_luaState.get())
		[
			luabind::scope(
				luabind::class_<mmo::RealmData>("RealmData")
					.def_readonly("id", &mmo::RealmData::id)
					.def_readonly("name", &mmo::RealmData::name)),

			luabind::scope(
				luabind::class_<mmo::CharacterView>("CharacterView")
					.def_readonly("guid", &mmo::CharacterView::GetGuid)
					.def_readonly("name", &mmo::CharacterView::GetName)),

			luabind::scope(
				luabind::class_<LoginConnector>("LoginConnector")
					.def("GetRealms", &LoginConnector::GetRealms, luabind::return_stl_iterator())),

			luabind::scope(
				luabind::class_<RealmConnector>("RealmConnector")
	               .def("ConnectToRealm", &RealmConnector::ConnectToRealm)
	               .def("GetCharViews", &RealmConnector::GetCharacterViews, luabind::return_stl_iterator())
	               .def("GetRealmName", &RealmConnector::GetRealmName)
	               .def("CreateCharacter", &RealmConnector::CreateCharacter)
	               .def("DeleteCharacter", &RealmConnector::DeleteCharacter)),

			luabind::scope(
				luabind::class_<LoginState>("LoginState")
					.def("EnterWorld", &LoginState::EnterWorld)),

			luabind::def("RunConsoleCommand", &Script_RunConsoleCommand),
			luabind::def("EnterWorld", &Script_EnterWorld),
			luabind::def("print", &Script_Print)
		];

		// Set login connector instance
		luabind::globals(m_luaState.get())["loginConnector"] = &m_loginConnector;
		luabind::globals(m_luaState.get())["realmConnector"] = &m_realmConnector;
		luabind::globals(m_luaState.get())["loginState"] = m_loginState.get();

		// Functions now registered
		m_globalFunctionsRegistered = true;
	}


}
