#pragma once
#include "..\Blam\Cache\DataTypes\DataTypes.h"
#include "..\Blam\Cache\TagGroups.hpp"
#include "..\Blam\Cache\DataTypes\TagBlock.h"

/*********************************************************************
* name: ai_mission_dialogue
* group_tag : mdlg
* header size : 8
* *********************************************************************/

#pragma pack(push,1)
struct s_ai_mission_dialogue_group_definition :TagGroup<'mdlg'>
{
	struct s_lines_block
	{
		string_id name;//0x0
		struct s_variants_block
		{
			string_id variant_designation;//0x0
			tag_reference sound;//0x4
			string_id sound_effect;//0xC
		};
		TAG_BLOCK_SIZE_ASSERT(s_variants_block, 0x10);
		tag_block<s_variants_block> variants;//0x4
		string_id default_sound_effect;//0xC
	};
	TAG_BLOCK_SIZE_ASSERT(s_lines_block, 0x10);
	tag_block<s_lines_block> lines;//0x0
};
TAG_GROUP_SIZE_ASSERT(s_ai_mission_dialogue_group_definition, 0x8);

#pragma pack(pop)

