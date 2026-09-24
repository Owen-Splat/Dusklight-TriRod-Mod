#include "mods/service.hpp"
#include "mods/svc/actor.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/hook.h"
#include "mods/svc/stage.h"
#include "mods/svc/ui.h"

#include "echo_menu.hpp"
#include "mod_data.hpp"
#include <algorithm>
#include <format>

// Game includes
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_crod.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "d/d_particle.h"
#include "f_op/f_op_actor_iter.h"
#include "JSystem/JGeometry.h"

DEFINE_MOD();
IMPORT_SERVICE(ActorService, svc_actor);
IMPORT_SERVICE(StageService, svc_stage);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(UiService, svc_ui);

// Hook into dominion rod connection
DEFINE_HOOK(&daCrod_c::execute, CopyActor);
DEFINE_HOOK(&daAlink_c::throwCopyRod, RodAction);
DEFINE_HOOK(&daAlink_c::procCopyRodSwing, SpawnEchoes);
DEFINE_HOOK(&daAlink_c::procCopyRodSubject, RemoveEchoes);
DEFINE_HOOK(&daAlink_c::checkCopyRodTopUse, AllowUseRod);

const u8 MAX_ECHOES = 3;
s16 copiedActorName = -1;
ActorId createdActorId = 0;
std::vector<ActorId> activeEchoes;
std::vector<ActorId> pendingEchoes;

bool spawningEcho = false;
u8 swingFrame = 0;
bool flashUp = true;
u8 flashValue = 0;

ActorId controlActor = 0;
s16 controlActorName = -1;
cXyz controlDistance(0.0f, 0.0f, 0.0f);

void* SearchRodTargets(fopAc_ac_c* actor, void* data) {
    if (!actor) {
        return nullptr;
    }

    daCrod_c* copyRod = (daCrod_c*)data;
    float xzDist = fopAcM_searchActorDistanceXZ(copyRod, actor);
    float yDist = fopAcM_searchActorDistanceY(copyRod, actor);

    if (xzDist <= 100.0f && yDist <= 250.0f) {
        return actor;
    }

    return nullptr;
}

void PlaySoundEffect(uint32_t soundID) {
    auto* audioMgr = Z2AudioMgr::getInterface();
    if (audioMgr) {
        daAlink_c* link = daAlink_getAlinkActorClass();
        audioMgr->mSoundMgr.startSound(soundID, 0, reinterpret_cast<const JGeometry::TVec3<f32>*>(&link->current.pos));
    }
}

void SpawnVanishSmokeEffect(const cXyz& position, u16 effect) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    dPa_control_c* particleControl = g_dComIfG_gameInfo.play.getParticle();

    if (particleControl != nullptr) {
        cXyz pPos = position;
        pPos.y += 15.0f;
        cXyz scale(0.5f, 0.5f, 0.5f);
        csXyz rotation(0, 0, 0);
        GXColor altColor = { 0, 30, 0, 255 };
        GXColor effectColor = { 210, 210, 0, 255 };

        JPABaseEmitter* emitter = particleControl->set(
            2,
            effect,
            &pPos,
            nullptr,
            &rotation,
            &scale,
            0xff,
            nullptr,
            fopAcM_GetRoomNo(link),
            &effectColor,
            &altColor,
            nullptr,
            1.0f
        );

        if (emitter) {
            emitter->setRate(1.0f);
            emitter->setDirectionalSpeed(-1.0f);
            emitter->mEmitCount = 50;
        }
    }
}

// Spawns the echo
// Currently just spawns the last actor the player copied
bool SpawnActor() {
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link) {
        // Spawn the echo a set distance in front of link
        float distance = 250.0f;
        float angleRad = (float)link->current.angle.y * (2.0f * M_PI / 65535.0f);
        float offsetX = distance * sinf(angleRad);
        float offsetZ = distance * cosf(angleRad);

        // Perform a simple raycast to snap it to the ground
        // If there is no ground, nothing will spawn
        cXyz startPos(link->current.pos.x + offsetX, link->current.pos.y + 200.0f, link->current.pos.z + offsetZ);
        cXyz endPos(link->current.pos.x + offsetX, link->current.pos.y - 100.0f, link->current.pos.z + offsetZ);
        dBgS_ObjLinChk lineChk;
        lineChk.Set(&startPos, &endPos, link);
        if (dComIfG_Bgsp().LineCross(&lineChk)) {
            // Keep echo limit at 3, despawn oldest one
            while (activeEchoes.size() >= MAX_ECHOES) {
                fopAc_ac_c* eActor = fopAcM_SearchByID(activeEchoes.front());
                if (eActor) {
                    SpawnVanishSmokeEffect(eActor->current.pos, 0x833F);
                }
                svc_actor->delete_actor(mod_ctx, activeEchoes.front());
                activeEchoes.erase(activeEchoes.begin());
            }
            
            const EchoObject* echo = FindEcho(copiedActorName);

            // Now spawn the new one
            ActorSpawnParams spawnParams = {
                .parameters = echo->paramaters,
                .argument = echo->argument,
                .room_num = fopAcM_GetRoomNo(link),
                .position = { lineChk.GetCross().x, lineChk.GetCross().y, lineChk.GetCross().z },
                .angle = { link->current.angle.x, link->current.angle.y, link->current.angle.z },
                .scale = { 0.9f, 0.9f, 0.9f }
            };
            svc_actor->create_actor(mod_ctx, copiedActorName, &spawnParams, &createdActorId);
            pendingEchoes.push_back(createdActorId);
            PlaySoundEffect(Z2SoundID::Z2SE_MAGIC_METER_DEC);
            return true;
        }
        else {
            PlaySoundEffect(Z2SoundID::Z2SE_AL_V_ZENTEN_FAIL);
            return false;
        }
    }
}

// Learn echo or bind the hitActor if valid
void OnRodHit(fopAc_ac_c* hitActor) {
    const EchoObject* echo = FindEcho(hitActor->profile->name);
    if (echo) {
        PlaySoundEffect(Z2SoundID::Z2SE_SY_ITEM_SET_B);
        copiedActorName = hitActor->profile->name;
        UiToastDesc toast = UI_TOAST_DESC_INIT;
        toast.type = "success";
        toast.title_rml = "Tri:";
        toast.body_rml = "<span>You learned a new echo!</span>";
        toast.duration_ms = 3000;
        svc_ui->push_toast(mod_ctx, &toast);
        return;
    }

    if (CheckBindable(hitActor->group)) {
        PlaySoundEffect(Z2SoundID::Z2SE_CSTATUE_S_START);
        daAlink_c* link = daAlink_getAlinkActorClass();
        controlActor = hitActor->id;
        controlActorName = hitActor->profile->name;
        controlDistance = hitActor->current.pos - link->current.pos;
        hitActor->tevStr.TevKColor.g = 45; // Not all objects use this
    }
}

// Hook into frame loop of the dominion rod projectile
// Grab actor that it hits (now distance detected rather than collision)
// Then we pass it to OnRodHit to determine the action
HookAction on_copy_actor_pre(ModContext* ctx, void* args, void*, void*) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    daCrod_c* copyRod = (daCrod_c*)link->getCopyRodActor();
    if (copyRod) {
        if (fopAcM_GetParam(copyRod) != 6) {
            if (!link->checkCopyRodRevive()) {
                fopAc_ac_c* hitActor = (fopAc_ac_c*)fopAcIt_Judge((fopAcIt_JudgeFunc)SearchRodTargets, copyRod);
                if (hitActor) {
                    if (copiedActorName == -1 && controlActor == 0) {
                        OnRodHit(hitActor);
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
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (copiedActorName != -1) {
        link->procCopyRodSwingInit();
        spawningEcho = true;
        return HOOK_SKIP_ORIGINAL;
    }

    fopAc_ac_c* cActor = fopAcM_SearchByID(controlActor);
    if (cActor && cActor->profile->name == controlActorName) {
        float currentFrame = link->mUnderFrameCtrl[0].getFrame();
        if (currentFrame >= 5.0f) {
            PlaySoundEffect(Z2SoundID::Z2SE_CSTATUE_S_STOP);
            cActor->tevStr.TevKColor.g = 0;
            controlActor = 0;
            link->procCopyRodSwingInit();
            return HOOK_SKIP_ORIGINAL;
        }
    }
    return HOOK_CONTINUE;
}

// Hook into the func for swinging the rod
// If spawning an echo, wait for part of the animation to play first
HookAction on_swing_pre(ModContext* ctx, void* args, void*, void*) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link && spawningEcho) {
        float currentFrame = link->mUnderFrameCtrl[0].getFrame();
        if (currentFrame >= 7.0f) {
            spawningEcho = false;
            if (SpawnActor()) {
                activeEchoes.push_back(createdActorId);
            }
        }
    }
    return HOOK_CONTINUE;
}

// Hook into the func for aiming the dominion rod
// Play an animation and remove any active echoes
HookAction on_sight_pre(ModContext* ctx, void* args, void*, void*) {
    if (activeEchoes.size() > 0) {
        daAlink_c* link = daAlink_getAlinkActorClass();
        link->procCopyRodReviveInit();
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

// Hook into the func that checks if you can use the rod
// Force it to always true while this mod is active
void check_rod_use_replace(ModContext*, void* args, void* retval, void*) {
    *static_cast<bool*>(retval) = true;
}

extern "C" {
    MOD_EXPORT ModResult mod_initialize(ModError*) {
        mods::hook::add_pre<CopyActor>(on_copy_actor_pre);
        mods::hook::add_pre<RodAction>(on_rod_pre);
        mods::hook::add_pre<SpawnEchoes>(on_swing_pre);
        mods::hook::add_pre<RemoveEchoes>(on_sight_pre);
        mods::hook::replace<AllowUseRod>(check_rod_use_replace);

        // Add a chest containing the rod
        // It is placed outside of Link's house
        // Seems to automatically delete when the mod is disabled (on map reload)
        stage_actor_data_class record = {
            .name = "tboxA1",
            .base = {
                .parameters = 0xFF100000,
                .position = {1534.0f, 811.4f, -3360.0f},
                .angle = {0, 0, 0x4600},
                .setID = 0xFFFF
            }
        };
        StageActorHandle handle{};
        svc_stage->add_actor(mod_ctx, "F_SP103", 1, -1, &record, sizeof(record), &handle);
        return MOD_OK;
    }

    MOD_EXPORT ModResult mod_update(ModError*) {
        daAlink_c* link = daAlink_getAlinkActorClass();
        if (link) {
            // Match controlled actors movements with Link
            fopAc_ac_c* cActor = fopAcM_SearchByID(controlActor);
            if (cActor && cActor->profile->name == controlActorName) {
                cActor->current.pos.x = link->current.pos.x + controlDistance.x;
                cActor->current.pos.y = link->current.pos.y + controlDistance.y;
                cActor->current.pos.z = link->current.pos.z + controlDistance.z;

                // Ground check for Y pos
                cXyz startPos(cActor->current.pos.x, cActor->current.pos.y + 100.0f, cActor->current.pos.z);
                cXyz endPos(cActor->current.pos.x, cActor->current.pos.y - 100.0f, cActor->current.pos.z);
                dBgS_ObjLinChk lineChk;
                lineChk.Set(&startPos, &endPos, cActor);
                if (dComIfG_Bgsp().LineCross(&lineChk)) {
                    cActor->current.pos.y = lineChk.GetCross().y;
                }

                // Also force the controlled actor to be focused at some point before release
            }

            // Clear echoes when the revive animation has fully played out
            if (link->mProcID == daAlink_c::PROC_COPY_ROD_REVIVE) {
                if (link->checkAnmEnd(&link->mUnderFrameCtrl[0])) {
                    link->procWaitInit();
                    for (auto id : activeEchoes) {
                        fopAc_ac_c* eActor = fopAcM_SearchByID(id);
                        if (eActor) {
                            SpawnVanishSmokeEffect(eActor->current.pos, 0x833F);
                        }
                        svc_actor->delete_actor(mod_ctx, id);
                    }
                    PlaySoundEffect(Z2SoundID::Z2SE_MAGIC_METER_FINISH);
                    activeEchoes.clear();
                    copiedActorName = -1;
                }
            }
        }

        // apply the yellow echo tint once loaded
        for (auto it = pendingEchoes.begin(); it != pendingEchoes.end(); ) {
            fopAc_ac_c* pActor = fopAcM_SearchByID(*it);
            if (pActor) {
                activeEchoes.push_back(*it);
                while (activeEchoes.size() > MAX_ECHOES) {
                    svc_actor->delete_actor(mod_ctx, activeEchoes.front());
                    activeEchoes.erase(activeEchoes.begin());
                    flashValue = 0;
                    flashUp = true;
                }
                it = pendingEchoes.erase(it);
                pActor->tevStr.TevKColor.r = 30;
                pActor->tevStr.TevKColor.g = 30;
                pActor->tevStr.TevKColor.b = 0;
            }
            else {
                ++it;
            }
        }

        // free up slots in our echo queue if they are naturally deleted
        activeEchoes.erase(
            std::remove_if(activeEchoes.begin(), activeEchoes.end(), [](ActorId id) {
                return fopAcM_SearchByID(id) == nullptr;
            }), activeEchoes.end());

        // reset yellow tint if one of them was previously flashing
        for (auto id : activeEchoes) {
            fopAc_ac_c* eActor = fopAcM_SearchByID(id);
            if (eActor) {
                eActor->tevStr.TevKColor.b = 0;
            }
        }

        // if there are 3 active echoes, make the oldest one flash whiteish
        if (activeEchoes.size() >= MAX_ECHOES) {
            if (flashUp) {
                flashValue += 3;
                if (flashValue >= 45) {
                    flashUp = false;
                }
            }
            else {
                flashValue -= 3;
                if (flashValue <= 0) {
                    flashUp = true;
                }
            }
            fopAc_ac_c* oldestActor = fopAcM_SearchByID(activeEchoes.front());
            if (oldestActor) {
                oldestActor->tevStr.TevKColor.b = flashValue;
            }
        }

        return MOD_OK;
    }

    MOD_EXPORT ModResult mod_shutdown(ModError*) {
        // delete all echoes if the mod is disabled
        for (auto id : activeEchoes) {
            svc_actor->delete_actor(mod_ctx, id);
        }
        activeEchoes.clear();
        copiedActorName = -1;

        // remove any binds
        fopAc_ac_c* cActor = fopAcM_SearchByID(controlActor);
        if (cActor && cActor->profile->name == controlActorName) {
            cActor->tevStr.TevKColor.g = 0;
        }
        controlActor = 0;

        return MOD_OK;
    }
}
