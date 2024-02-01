
#include "creature_ai_prepare_state.h"

#include "creature_ai.h"
#include "game_creature_s.h"
#include "universe.h"

namespace mmo
{
	CreatureAIPrepareState::CreatureAIPrepareState(CreatureAI& ai)
		: CreatureAIState(ai)
		, m_preparation(ai.GetControlled().GetTimers())
	{
	}

	CreatureAIPrepareState::~CreatureAIPrepareState()
	{
	}

	void CreatureAIPrepareState::OnEnter()
	{
		CreatureAIState::OnEnter();

		m_preparation.ended.connect([this]()
			{
				// Enter idle state
				GetAI().Idle();
			});

		// Start preparation timer (3 seconds)
		m_preparation.SetEnd(GetAsyncTimeMs() + constants::OneSecond * 6);

		// Watch for threat events to enter combat
		auto& ai = GetAI();
		m_onThreatened = GetControlled().threatened.connect(std::bind(&CreatureAI::OnThreatened, &ai, std::placeholders::_1, std::placeholders::_2));
	}

	void CreatureAIPrepareState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}

}
