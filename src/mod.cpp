#include "mods/svc/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/actor.h"
#include "mods/svc/hook.h"

// Game includes
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_crod.h"
#include "d/actor/d_a_cstaF.h"
#include "d/actor/d_a_cstatue.h"

DEFINE_MOD();

IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(ActorService, svc_actor);

// Hook into dominion rod connection
DEFINE_HOOK(&daCrod_c::execute, CopyActor);
DEFINE_HOOK(&daAlink_c::throwCopyRod, RodAction);
DEFINE_HOOK(&daAlink_c::procCopyRodSwingInit, SpawnEcho);

bool hasCopiedActor = false;
process_profile_definition* copiedProfile = nullptr;

void SpawnActor() {
    fopAc_ac_c* plr = dComIfGp_getPlayer(0);
    if (plr) {
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
        ActorId created_actor_id;
        svc_actor->create_actor(mod_ctx, copiedProfile->name, &spawnParams, &created_actor_id);
    }
}

HookAction on_copy_actor_pre(ModContext* ctx, void* args, void*, void*) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    daCrod_c* copyRod = (daCrod_c*)link->getCopyRodActor();
    if (copyRod != NULL) {
        if (fopAcM_GetParam(copyRod) != 6) {
            if (!link->checkCopyRodRevive()) {
                copyRod->mAtCps.SetAtType(AT_TYPE_CSTATUE_SWING);
                if (copyRod->mAtCps.ChkAtHit()) {
                    fopAc_ac_c* hit_ac = copyRod->mAtCps.GetAtHitAc();
                    if (link->checkCopyRodEquip() && hit_ac != NULL) {
                        copiedProfile = hit_ac->profile;
                        hasCopiedActor = true;
                        dComIfGp_getEvent()->setGtItm(0);
                        link->mProcID = daAlink_c::PROC_GET_ITEM;
                        const s16 eventIndex = dComIfGp_getEventManager().getEventIdx(link, "DEFAULT_GETITEM", 0xFF);
                        fopAcM_orderChangeEventId(link, eventIndex, 1, 0xFFFF);
                    }
                }
            }
        }
    }
    return HOOK_CONTINUE;
}

HookAction on_rod_pre(ModContext* ctx, void* args, void*, void*) {
    if (hasCopiedActor) {
        daAlink_c* link = daAlink_getAlinkActorClass();
        link->procCopyRodSwingInit();
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

HookAction on_swing_pre(ModContext* ctx, void* args, void*, void*) {
    if (hasCopiedActor) {
        hasCopiedActor = false;
        SpawnActor();
    }
    return HOOK_CONTINUE;
}

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    mods::hook::add_pre<CopyActor>(on_copy_actor_pre);
    mods::hook::add_pre<RodAction>(on_rod_pre);
    mods::hook::add_pre<SpawnEcho>(on_swing_pre);
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    return MOD_OK;
}
}
