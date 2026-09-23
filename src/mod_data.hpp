#pragma once
#include <types.h>
#include <algorithm>
#include <vector>
#include <string>
#include "f_pc/f_pc_name.h"

struct EchoObject {
	s16 profileName;
	std::string visibleName;
	uint32_t paramaters;
	int8_t argument;
};

inline const std::vector<EchoObject> EchoDatabase{
	{ fpcNm_Obj_Stone_e,		"Rock",			0,		0},
	{ fpcNm_OBJ_PUMPKIN_e,		"Pumpkin",		0,		0}
};

inline const EchoObject* FindEcho(s16 profileName) {
	auto it = std::find_if(EchoDatabase.begin(), EchoDatabase.end(),
		[profileName](const EchoObject& obj) {
			return obj.profileName == profileName;
		});

	return (it != EchoDatabase.end()) ? &(*it) : nullptr;
}

inline const std::vector<s16> BindDatabase{

};

inline bool CheckBindable(s16 profileName) {
	return std::find(BindDatabase.begin(), BindDatabase.end(), profileName) != BindDatabase.end();
}