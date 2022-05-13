// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "connection.h"
#include "player_controller.h"
#include "screen.h"
#include "game_states/game_state.h"

#include "base/signal.h"
#include "game/game_object_c.h"
#include "game_protocol/game_protocol.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/light.h"
#include "scene_graph/scene.h"
#include "scene_graph/world_grid.h"


namespace mmo
{
	namespace game
	{
		class IncomingPacket;
	}

	class LoginConnector;
	class RealmConnector;

	class Entity;
	class SceneNode;

	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class WorldState final
		: public GameState
	{
	public:
		/// @brief Creates a new instance of the WorldState class and initializes it.
		/// @param gameStateManager The game state manager that this state belongs to.
		/// @param realmConnector The connector which manages the connection to the realm server.
		explicit WorldState(GameStateMgr& gameStateManager, RealmConnector& realmConnector);

	public:
		/// @brief The default name of the world state
		static const std::string Name;

	public:
		/// @copydoc GameState::OnEnter
		void OnEnter() override;
		
		/// @copydoc GameState::OnLeave
		void OnLeave() override;
		
		/// @copydoc GameState::GetName
		[[nodiscard]] std::string_view GetName() const override;

	private:
		// EventLoop connections
		
		bool OnMouseDown(MouseButton button, int32 x, int32 y);

		bool OnMouseUp(MouseButton button, int32 x, int32 y);

		bool OnMouseMove(int32 x, int32 y);

		bool OnMouseWheel(int32 delta);
		
		bool OnKeyDown(int32 key);
		
		bool OnKeyUp(int32 key);

		void OnIdle(float deltaSeconds, GameTime timestamp);

		/// Called when the screen layer should be painted. Should paint the scene.
		void OnPaint();

	private:
		// Setup stuff

		void SetupWorldScene();

		void SetupPacketHandler();

		void RemovePacketHandler() const;
		
	private:
		//

		void OnRealmDisconnected();

		void OnEnterWorldFailed(game::player_login_response::Type error);

		void RegisterGameplayCommands() const;

		void RemoveGameplayCommands();

	private:
		// Gameplay command callbacks

		void ToggleAxisVisibility() const;

		void ToggleGridVisibility() const;

		void ToggleWireframe() const;

	private:
		// Network packet handlers

		PacketParseResult OnUpdateObject(game::IncomingPacket& packet);
		
		PacketParseResult OnCompressedUpdateObject(game::IncomingPacket& packet);

		PacketParseResult OnDestroyObjects(game::IncomingPacket& packet);

		PacketParseResult OnMovement(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		ScreenLayerIt m_paintLayer;
		scoped_connection_container m_realmConnections;
		scoped_connection_container m_inputConnections;
		Scene m_scene;
		std::unique_ptr<PlayerController> m_playerController;
		std::unique_ptr<AxisDisplay> m_debugAxis;
		std::unique_ptr<WorldGrid> m_worldGrid;
		std::map<uint64, std::shared_ptr<GameObjectC>> m_gameObjectsById;

		SceneNode* m_archNode { nullptr };
		SceneNode* m_towerLeftNode { nullptr };
		SceneNode* m_towerRightNode { nullptr };
		Entity* m_archEntity { nullptr };
		Entity* m_towerLeftEntity { nullptr };
		Entity* m_towerRightEntity { nullptr };
		SceneNode* m_groundNode { nullptr };
		Entity* m_groundEntity { nullptr };
		SceneNode* m_cloudsNode { nullptr };
		Entity* m_cloudsEntity { nullptr };
		Light* m_sunLight { nullptr };
	};
}
