#pragma once
#include <algorithm>
#include <vector>
#include <string>
#include <types.h>
#include "f_pc/f_pc_name.h"

struct EchoObject {
	s16 profileName;
	std::string visibleName;
	uint32_t parameters;
	int8_t argument;
};

// Just a rock for now until I map out stuff
// I would love to create enemy echoes but that would be a lot of custom actor stuff
inline const std::vector<EchoObject> EchoDatabase{
	{ fpcNm_Obj_Stone_e,		"Rock",			0,		0},
};

inline const EchoObject* FindEcho(s16 profileName) {
	auto it = std::find_if(EchoDatabase.begin(), EchoDatabase.end(),
		[profileName](const EchoObject& obj) {
			return obj.profileName == profileName;
		});

	return (it != EchoDatabase.end()) ? &(*it) : nullptr;
}

// For now, let the player grab anything grouped as an enemy or npc
// Will need to play through the game and make exceptions
inline bool CheckBindable(u8 group) {
	return group == 2 || group == 4;
}