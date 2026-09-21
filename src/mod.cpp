#include <algorithm>
#include <format>

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include "mods/svc/actor.h"
#include "mods/svc/hook.h"
#include "mods/svc/ui.h"

// Game includes
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_crod.h"
#include "d/actor/d_a_cstaF.h"
#include "d/actor/d_a_cstatue.h"
#include "d/actor/d_a_e_rdy.h"
#include "d/actor/d_a_ni.h"

DEFINE_MOD();

IMPORT_SERVICE(ActorService, svc_actor);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(UiService, svc_ui);

// Hook into dominion rod connection
DEFINE_HOOK(&daCrod_c::execute, CopyActor);
DEFINE_HOOK(&daAlink_c::throwCopyRod, RodAction);
DEFINE_HOOK(&daAlink_c::procCopyRodSwingInit, SpawnEcho);

#define MAX_ECHOES 3

process_profile_definition* copiedProfile = NULL;
ActorId createdActorId;
std::vector<fopAc_ac_c*> activeEchoes;
bool echoInitialized = false;
bool flashUp = true;
u8 flashValue = 0;

// Spawns the echo
// Currently just spawns the last actor the player copied
void SpawnActor(ModContext* ctx) {
    fopAc_ac_c* plr = dComIfGp_getPlayer(0);
    if (plr) {
        // Keep echo limit at 3, despawn oldest one
        while (activeEchoes.size() >= MAX_ECHOES) {
            fopAc_ac_c* oldestActor = fopAcM_SearchByID((activeEchoes.front()->id));
            if (oldestActor) {
                svc_actor->delete_actor(mod_ctx, activeEchoes.front()->id);
            }
            activeEchoes.erase(activeEchoes.begin());
            flashValue = 0;
        }

        // Spawn the echo a set distance in front of link
        // The angle is off when you change direction by aiming, FIX LATER
        float distance = 250.0f;
        float angleRad = (float)plr->current.angle.y * (2.0f * M_PI / 65535.0f);
        float offsetX = distance * sinf(angleRad);
        float offsetZ = distance * cosf(angleRad);
        ActorSpawnParams spawnParams = {
            .parameters = copiedProfile->parameters,
            .argument = 0,
            .room_num = fopAcM_GetRoomNo(plr),
            .position = { plr->current.pos.x + offsetX, plr->current.pos.y, plr->current.pos.z + offsetZ },
            .angle = { plr->current.angle.x, plr->current.angle.y, plr->current.angle.z },
            .scale = { 1.0f, 1.0f, 1.0f }
        };
        svc_actor->create_actor(mod_ctx, copiedProfile->name, &spawnParams, &createdActorId);
        echoInitialized = false;
    }
}

// Checks if the hitProfile is both a valid echo and one that the player does not already have
s16 validEchoes[] = {
    fpcNm_Obj_Stone_e,      // throwable rock
    fpcNm_NI_e,             // cucco
    fpcNm_Obj_Yobikusa_e,   // calling grass
    fpcNm_NPC_KAKASHI_e,    // training dummy
    fpcNm_HORSE_e,          // epona
    fpcNm_E_HB_e,           // deku baba
    fpcNm_E_OC_e,           // bokoblin
    fpcNm_E_BA_e,           // keese
    fpcNm_E_BI_e,           // bomb plant
    fpcNm_E_ST_e,           // skultula
};

// Unfinished, just allow all actors to function as echoes until I map out what I want
bool CheckNewEcho(process_profile_definition* hitProfile) {
    return true;
}

// Ideally play an item get animation when learning a new echo
// For now we just push a notification
void LearnEcho(ModContext* ctx, process_profile_definition* hitProfile) {
    if (!CheckNewEcho) {
        return;
    }
    //svc_item->give_item(ctx, NULL, dItemNo_NONE_e, 0);
    copiedProfile = hitProfile;
    UiToastDesc toast = UI_TOAST_DESC_INIT;
    toast.type = "success";
    toast.title_rml = "Tri:";
    std::string s1 = std::format("<span>Link! You learned the {} echo!</span>", hitProfile->name);
    toast.body_rml = s1.c_str();
    toast.duration_ms = 3000;
    svc_ui->push_toast(mod_ctx, &toast);
}

// Hook into frame loop of the dominion light ball
// Grab actor that it hits and interrupt with the LearnEcho animation if it is new
HookAction on_copy_actor_pre(ModContext* ctx, void* args, void*, void*) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    daCrod_c* copyRod = (daCrod_c*)link->getCopyRodActor();
    if (copyRod) {
        if (fopAcM_GetParam(copyRod) != 6) {
            if (!link->checkCopyRodRevive()) {
                copyRod->mAtCps.SetAtType(AT_TYPE_NORMAL_SWORD);
                if (copyRod->mAtCps.ChkAtHit()) {
                    fopAc_ac_c* hit_ac = copyRod->mAtCps.GetAtHitAc();
                    if (link->checkCopyRodEquip() && hit_ac) {
                        if (hit_ac->profile != copiedProfile) {
                            LearnEcho(ctx, hit_ac->profile);
                        }
                    }
                }
            }
        }
    }
    return HOOK_CONTINUE;
}

// Hook into the func for throwing the dominion light ball
// If the player has an echo equipped, forcibly enter the swing state
HookAction on_rod_pre(ModContext* ctx, void* args, void*, void*) {
    if (copiedProfile) {
        daAlink_c* link = daAlink_getAlinkActorClass();
        link->procCopyRodSwingInit();
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

// Hook into dominion swing init
// Spawn the echo and unequip it
HookAction on_swing_pre(ModContext* ctx, void* args, void*, void*) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (!link->getCopyRodControllActor()) {
        if (copiedProfile) {
            SpawnActor(ctx);
            copiedProfile = NULL; // temp until echo menu is built
        }
    }
    return HOOK_CONTINUE;
}

void ChangeEchoBehavior(fopAc_ac_c* pActor) {
    //// Set group to Player to see if it distracts enemies
    //pActor->group = fopAc_PLAYER_e;

    // Yellow tint on echoes
    // We can edit it on a case by case basis if needed
    pActor->tevStr.TevKColor.r = 30;
    pActor->tevStr.TevKColor.g = 30;

    //// Add any needed parameters to actors depending on what it is
    //switch (fpcM_GetProfName(pActor)) {
    //    case fpcNm_NI_e: {
    //        ni_class* eActor = reinterpret_cast<ni_class*>(pActor);
    //        if (eActor) {
    //            eActor->mColor = daNi_color::COLOR_GOLD; // make cucco golden as a test
    //        }
    //        break;
    //    }
    //    default:
    //        break;
    //}
}

extern "C" {
    MOD_EXPORT ModResult mod_initialize(ModError*) {
        mods::hook::add_pre<CopyActor>(on_copy_actor_pre);
        mods::hook::add_pre<RodAction>(on_rod_pre);
        mods::hook::add_pre<SpawnEcho>(on_swing_pre);
        return MOD_OK;
    }

    MOD_EXPORT ModResult mod_update(ModError*) {
        if (!echoInitialized) {
            fopAc_ac_c* pActor = fopAcM_SearchByID(createdActorId);
            if (pActor) {
                ChangeEchoBehavior(pActor);
                echoInitialized = true;
                activeEchoes.push_back(pActor);
            }
        }
        // free up slots in our echo queue if they are naturally deleted
        activeEchoes.erase(std::remove(activeEchoes.begin(), activeEchoes.end(), nullptr), activeEchoes.end());

        // if there are 3 active echoes, make the oldest one flash whiteish
        if (activeEchoes.size() >= MAX_ECHOES) {
            if (flashUp) {
                flashValue += 2;
                if (flashValue >= 30) {
                    flashUp = false;
                }
            }
            else {
                flashValue -= 2;
                if (flashValue <= 0) {
                    flashUp = true;
                }
            }
            activeEchoes.front()->tevStr.TevKColor.b = flashValue;
        }
        return MOD_OK;
    }

    MOD_EXPORT ModResult mod_shutdown(ModError*) {
        // delete all echoes if the mod is disabled
        for (auto echo : activeEchoes) {
            svc_actor->delete_actor(mod_ctx, echo->id);
        }
        activeEchoes.erase(std::remove(activeEchoes.begin(), activeEchoes.end(), nullptr), activeEchoes.end());
        return MOD_OK;
    }
}
