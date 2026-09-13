#pragma once

#include <cstdint>
#include <windows.h>

#ifndef li
#define li(fn) fn
#endif

namespace project_x
{
    inline std::uintptr_t g_base = 0;

    inline auto base() -> std::uintptr_t
    {
        if (!g_base)
            g_base = reinterpret_cast<std::uintptr_t>(li(GetModuleHandleA)(nullptr));
        return g_base;
    }

    inline auto rebase(const std::uintptr_t rva) -> std::uintptr_t
    {
        return rva ? base() + rva : 0;
    }

    namespace rva
    {
        inline constexpr std::uintptr_t print = 0x1cab4b0;
        inline constexpr std::uintptr_t get_tss_data = 0x4250;
        inline constexpr std::uintptr_t tls_identity_struct = 0x815e708;
        inline constexpr std::uintptr_t task_defer = 0x4319b60;
        inline constexpr std::uintptr_t script_context_resume = 0x4260f50;
        inline constexpr std::uintptr_t dm_lock_owner = 0x43e0;
        inline constexpr std::uintptr_t luac_step = 0x2719020;
        inline constexpr std::uintptr_t whsj_step = 0x428cea0;
        inline constexpr std::uintptr_t task_scheduler = 0x8b5cee8;
        inline constexpr std::uintptr_t push_instance = 0x41a5f20;

        inline constexpr std::uintptr_t fire_mouse_click = 0x3b4d9f0;
        inline constexpr std::uintptr_t fire_right_mouse_click = 0x3b4db90;
        inline constexpr std::uintptr_t fire_mouse_hover = 0x3b4efe0;
        inline constexpr std::uintptr_t fire_mouse_hover_leave = 0x3b4f180;

        inline constexpr std::uintptr_t weak_thread_ref_vftable = 0x6a4e640;

        inline constexpr std::uintptr_t func_script_slot_vft = 0x6c7fbf8;
        inline constexpr std::uintptr_t func_script_slot_vft_alt = 0x6c7fac0;
        inline constexpr std::uintptr_t wait_script_slot_vft = 0x6c7f910;
        inline constexpr std::uintptr_t weak_object_ref_vft = 0x6a452e8;

        inline constexpr std::uintptr_t rbxscriptsignalt = 0x8daad00;
        inline constexpr std::uintptr_t rbxscriptconnectiont = 0x88b51a4;

        inline constexpr std::uintptr_t lua_arguments_get = 0x422a730;
        inline constexpr std::uintptr_t raise_event_invocation = 0x1cf7cb0;
        inline constexpr std::uintptr_t fire_touch_interest = 0xa6dfa0;
        inline constexpr std::uintptr_t fire_proximity_prompt = 0x3102650;

        inline constexpr std::uintptr_t get_capabilities = 0x1ce6760;
        inline constexpr std::uintptr_t find_property_map = 0x1ce4be0;
        inline constexpr std::uintptr_t property_ktable = 0x80bbe20;

        inline constexpr std::uintptr_t lua_async_callback_vtable = 0x6c7fea8;
        inline constexpr std::uintptr_t lua_sync_callback_vtable = 0x6c7fee0;
        inline constexpr std::uintptr_t callback_binder_vtable = 0;

        inline constexpr std::uintptr_t shared_string_create = 0x4925190;
        inline constexpr std::uintptr_t shared_string_release = 0xa47230;

        inline constexpr std::uintptr_t fflag_registry = 0x887ca40;

        inline constexpr std::uintptr_t getset_vtable_int = 0x6cb1300;
        inline constexpr std::uintptr_t getset_vtable_dint = 0x6cb1418;
        inline constexpr std::uintptr_t getset_vtable_string = 0x6cb1530;
        inline constexpr std::uintptr_t getset_vtable_bool = 0x6cb1648;
        inline constexpr std::uintptr_t getset_vtable_dflag = 0x6cb10d0;
        inline constexpr std::uintptr_t getset_vtable_channel = 0x6cb11e8;

        inline constexpr std::uintptr_t udatadirect_indexf = 0x41a3d10;
        inline constexpr std::uintptr_t udatadirect_newindexf = 0x41a4bf0;

        inline constexpr std::uintptr_t instance_index = 0x41a1eb0;
        inline constexpr std::uintptr_t instance_newindex = 0x41a1710;

        inline constexpr std::uintptr_t instance_property_get = 0x419bec0;
        inline constexpr std::uintptr_t instance_property_set = 0x419d8b0;

        inline constexpr std::uintptr_t event_once = 0x4185bd0;
        inline constexpr std::uintptr_t event_wait = 0x4185d70;
        inline constexpr std::uintptr_t connection_disconnect = 0x4186a50;

        inline constexpr std::uintptr_t instance_udata_tag = 0x8dab760;
        inline constexpr std::uintptr_t lua_newuserdatatagged = 0x26fa460;
    }

    namespace rva_vm
    {
        inline constexpr std::uintptr_t luaD_throw = 0x2709720;
        inline constexpr std::uintptr_t luaD_callint = 0x26f9410;
        inline constexpr std::uintptr_t luaD_performcally = 0x2709800;
        inline constexpr std::uintptr_t luaH_dummynode = 0x63cab08;
        inline constexpr std::uintptr_t nilobject = 0x63cdf48;
        inline constexpr std::uintptr_t opcode_lookup_table = 0x6e26c70;
        inline constexpr std::uintptr_t lua_vm_load = 0x41b3b50;
        inline constexpr std::uintptr_t udatadirect_namecall_frealson = 0x41a5290;
        inline constexpr std::uintptr_t udatadirect_registrar = 0x26fb260;
        inline constexpr std::uintptr_t luau_execute = 0x2736e10;
        inline constexpr std::uintptr_t luau_execute_singlestep = 0x272aa70;

        inline constexpr std::uintptr_t lua_typename = 0;
        inline constexpr std::uintptr_t luaT_objtypename = 0x2725290;
        inline constexpr std::uintptr_t luaT_objtypenamestr = 0x2725420;
        inline constexpr std::uintptr_t luaG_getline = 0x2722680;
    }

    namespace rva_flag
    {
        inline constexpr std::uintptr_t LuauCIProto = 0x7C51288;
        inline constexpr std::uintptr_t LuauCallFeedback = 0x7C51248;
        inline constexpr std::uintptr_t LuauBackedgeHeapCheck = 0x7C512A8;
        inline constexpr std::uintptr_t LuauPromoteProto = 0x7C51228;
        inline constexpr std::uintptr_t DebugLuauUserDefinedClassesRuntime = 0x7C512C8;
        inline constexpr std::uintptr_t LuauFastpcall = 0x7C51308;
    }
}
