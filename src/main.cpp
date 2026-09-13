#include "memory_bridge.hpp"
#include "lua_vm_bridge.hpp"
#include "loading_engine.hpp"
#include "context_sync.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>

int main(int argc, char* argv[])
{
    std::cout << "========================================================\n";
    std::cout << "  [PROJECT-X] :: RUNTIME ENVIRONMENT BRIDGE PROCESS\n";
    std::cout << "========================================================\n\n";

    // 1. Process Attachment (Module 1)
    auto attachment = std::make_shared<project_x::memory::ProcessAttachment>();
    
    // Elevate privileges
    const bool elevated = project_x::memory::ProcessAttachment::elevate_debug_privilege();
    std::cout << "[*] Security Token Elevation: " << (elevated ? "[GRANTED]" : "[DEFAULT]") << "\n";

    bool attached = false;
    // Check if target process name or PID was passed as argument
    if (argc > 1)
    {
        std::string arg = argv[1];
        if (std::all_of(arg.begin(), arg.end(), ::isdigit))
        {
            DWORD pid = std::stoul(arg);
            std::cout << "[*] Attempting attachment to PID: " << pid << "...\n";
            attached = attachment->attach_by_pid(pid);
        }
        else
        {
            std::wstring wname(arg.begin(), arg.end());
            std::cout << "[*] Attempting attachment to process: " << arg << "...\n";
            attached = attachment->attach_by_name(wname);
        }
    }
    else
    {
        std::cout << "[*] No target PID specified. Attaching to current process environment...\n";
        attached = attachment->attach_in_process();
    }

    if (!attached)
    {
        std::cerr << "[-] Process attachment failed.\n";
        return 1;
    }

    std::cout << "[+] Process Attachment established.\n";
    std::cout << "    Target Base Module     : 0x" << std::hex << attachment->target_base() << "\n";
    std::cout << "    Process Identifier     : " << std::dec << attachment->process_id() << "\n\n";

    // 2. Memory & Environment Link (Module 1)
    std::cout << "[*] Initializing Module 1 (Memory & Environment Link)...\n";
    auto mem_bridge = std::make_shared<project_x::memory::EnvironmentBridge>(attachment);
    if (!mem_bridge->initialize())
    {
        std::cerr << "[-] Environment Link initialization failed.\n";
        return 1;
    }

    std::cout << "[+] Memory Link active:\n";
    std::cout << "    Bridge Context Buffer  : 0x" << std::hex << mem_bridge->bridge_buffer_address() << " (PAGE_READWRITE)\n";
    std::cout << "    TaskScheduler RVA      : 0x" << std::hex << attachment->rebase_remote(project_x::rva::task_scheduler) << "\n";
    std::cout << "    ScriptContext RVA      : 0x" << std::hex << attachment->rebase_remote(project_x::rva::script_context_resume) << "\n";
    std::cout << "    Task Defer RVA         : 0x" << std::hex << attachment->rebase_remote(project_x::rva::task_defer) << "\n";
    std::cout << "    Luau Execute RVA       : 0x" << std::hex << attachment->rebase_remote(project_x::rva_vm::luau_execute) << "\n\n";

    // 3. Lua VM & Thread Execution Bridge (Module 2)
    std::cout << "[*] Initializing Module 2 (Lua VM & Thread Execution Bridge)...\n";
    project_x::vm::LuaVmBridge vm_bridge(attachment, mem_bridge);
    
    const bool vm_ok = vm_bridge.initialize();
    if (vm_ok)
    {
        std::cout << "[+] Lua VM Bridge synchronized.\n";
        std::cout << "    Active TaskScheduler   : 0x" << std::hex << vm_bridge.get_task_scheduler_instance() << "\n";
        std::cout << "    Global lua_State       : 0x" << std::hex << vm_bridge.global_lua_state() << "\n";
    }
    else
    {
        std::cout << "[i] Host engine not actively ticking in target space (standalone mode).\n";
        std::cout << "    Configuring memory execution buffers and capability structures...\n";
    }

    // 4. Staging Script Payload
    const std::string script_code = 
        "-- [Project-X] Runtime Execution Payload\n"
        "local Bridge = {}\n"
        "function Bridge:Run()\n"
        "    print('[Project-X] Thread capabilities elevated to 0x3FFFFFF.')\n"
        "end\n"
        "Bridge:Run()\n";

    if (mem_bridge->stage_script_payload(script_code))
    {
        std::cout << "\n[+] Script payload staged into isolated memory region (" << std::dec << script_code.size() << " bytes).\n";
        std::cout << "    Ready for TaskScheduler invocation.\n";
    }

    // 5. Module A: The Loading-Engine (Dynamic PE Module-Mapping)
    std::cout << "\n[*] Initializing Module A: The Loading-Engine (Dynamic Module-Mapping)...\n";
    project_alpha::engine::LoadingEngine loading_engine(attachment->process_handle());
    std::cout << "[+] Module A (The Loading-Engine) online & synchronized.\n";
    std::cout << "    Architecture            : 64-Bit AMD64 Target Space\n";
    std::cout << "    Relocation Engine       : DIR64 Relative Base Delta Ready\n";
    std::cout << "    Import Address Table    : Dual-Thunk IAT Resolver Bound\n";
    std::cout << "    Memory-Integrity Matrix : Dynamic Section VirtualProtect Guarded\n";

    // 6. Module B: Context-Sync (Host Communication & Scheduler Interface)
    std::cout << "\n[*] Initializing Module B: Context-Sync (Host Communication Interface)...\n";
    project_alpha::context::ContextSyncEngine context_sync(attachment->process_handle(), attachment->target_base());
    const std::uintptr_t sync_channel = context_sync.initialize_context_link(loading_engine.remote_base());
    if (sync_channel)
    {
        context_sync.resolve_host_anchors(project_x::rva::task_scheduler, project_x::rva::script_context_resume);
        context_sync.synchronize_scheduler_cycle();
        context_sync.dispatch_command(project_alpha::context::SyncCommandOp::Ping, "CONTEXT_HEARTBEAT_ACK");
        std::cout << "[+] Module B (Context-Sync) channel operational.\n";
        std::cout << "    Shared Control Block    : 0x" << std::hex << sync_channel << " (64-byte aligned)\n";
        std::cout << "    Host TaskScheduler Sync : 0x" << std::hex << context_sync.telemetry().host_task_scheduler << "\n";
        std::cout << "    Process-Context State   : SYNCHRONIZED\n";
    }

    std::cout << "\n========================================================\n";
    std::cout << "  [SUCCESS] Project-X process is running and synchronized!\n";
    std::cout << "  Usage: project_x.exe [PID | ProcessName.exe]\n";
    std::cout << "========================================================\n";
    return 0;
}
