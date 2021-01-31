#pragma once
#include "Blam/Maths/real_math.h"
#include "Blam/Engine/Players/PlayerControls.h"

using namespace Blam::EngineDefinitions::Players;
namespace PlayerControl
{
	void Init();
	void ApplyHooks();
	s_player_actions GetPlayerActions(int player_index);
	Blam::EngineDefinitions::Players::s_player_control* GetControls(int local_player_index);
}
