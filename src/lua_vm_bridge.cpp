#include "lua_vm_bridge.hpp"
#include <iostream>
#include <vector>

namespace project_x::vm
{
    // Common internal offsets for TaskScheduler & Job layout
    namespace scheduler_offsets
    {
        constexpr std::uintptr_t jobs_vector_start = 0x198;
        constexpr std::uintptr_t jobs_vector_end   = 0x1A0;
        constexpr std::uintptr_t job_name_offset   = 0x90;
        constexpr std::uintptr_t job_step_rate     = 0xC0;
    }

    // Common internal offsets for ScriptContext & ExtraSpace
    namespace thread_offsets
    {
        constexpr std::uintptr_t script_context_lua_state = 0x1F8;
        constexpr std::uintptr_t lua_extraspace_identity  = 0x30;
        constexpr std::uintptr_t lua_extraspace_caps      = 0x48;
    }

    LuaVmBridge::LuaVmBridge(std::shared_ptr<memory::ProcessAttachment> attachment,
                             std::shared_ptr<memory::EnvironmentBridge> memory_bridge)
        : attachment_(std::move(attachment)), memory_bridge_(std::move(memory_bridge))
    {
    }

    LuaVmBridge::~LuaVmBridge() = default;

    bool LuaVmBridge::initialize()
    {
        if (!attachment_ || !attachment_->is_attached())
            return false;

        // 1. Resolve and cache the active TaskScheduler instance
        task_scheduler_instance_ = get_task_scheduler_instance();
        if (task_scheduler_instance_ == 0)
            return false;

        // 2. Tune runtime engine flags
        configure_engine_flags();

        // 3. Resolve engine script context & global lua state
        global_lua_state_ = resolve_global_state();

        initialized_ = true;
        return true;
    }

    std::uintptr_t LuaVmBridge::get_task_scheduler_instance() const
    {
        if (!attachment_ || !attachment_->is_attached())
            return 0;

        const std::uintptr_t scheduler_holder = attachment_->rebase_remote(rva::task_scheduler);
        if (scheduler_holder == 0)
            return 0;

        std::uintptr_t scheduler_instance = 0;
        if (!attachment_->read(scheduler_holder, scheduler_instance) || scheduler_instance == 0)
            return 0;

        return scheduler_instance;
    }

    std::vector<TaskSchedulerJob> LuaVmBridge::enumerate_scheduler_jobs() const
    {
        std::vector<TaskSchedulerJob> jobs;
        if (!task_scheduler_instance_ || !attachment_)
            return jobs;

        std::uintptr_t start_ptr = 0;
        std::uintptr_t end_ptr = 0;

        if (!attachment_->read(task_scheduler_instance_ + scheduler_offsets::jobs_vector_start, start_ptr) ||
            !attachment_->read(task_scheduler_instance_ + scheduler_offsets::jobs_vector_end, end_ptr))
        {
            return jobs;
        }

        const std::size_t job_count = (end_ptr > start_ptr) ? (end_ptr - start_ptr) / sizeof(std::uintptr_t) : 0;
        if (job_count == 0 || job_count > 512)
            return jobs;

        for (std::size_t i = 0; i < job_count; ++i)
        {
            std::uintptr_t job_addr = 0;
            if (!attachment_->read(start_ptr + (i * sizeof(std::uintptr_t)), job_addr) || job_addr == 0)
                continue;

            TaskSchedulerJob job_desc{};
            job_desc.instance = job_addr;
            attachment_->read(job_addr, job_desc.vftable);

            // Read job name
            std::uintptr_t name_ptr = 0;
            if (attachment_->read(job_addr + scheduler_offsets::job_name_offset, name_ptr) && name_ptr != 0)
            {
                char name_buf[64]{};
                attachment_->read_raw(name_ptr, name_buf, sizeof(name_buf) - 1);
                job_desc.name = std::string(name_buf);
            }

            // Read step rate
            attachment_->read(job_addr + scheduler_offsets::job_step_rate, job_desc.step_rate);
            jobs.push_back(std::move(job_desc));
        }

        return jobs;
    }

    std::uintptr_t LuaVmBridge::resolve_global_state()
    {
        if (!attachment_)
            return 0;

        const auto jobs = enumerate_scheduler_jobs();
        for (const auto& job : jobs)
        {
            // Identify ScriptContext job (WaitingHybridScriptsJob)
            if (job.name.find("WaitingHybridScriptsJob") != std::string::npos ||
                job.name.find("WaitingScriptsJob") != std::string::npos)
            {
                // In WaitingHybridScriptsJob, ScriptContext pointer is stored at offset +0x1F0
                std::uintptr_t script_context_ptr = 0;
                if (attachment_->read(job.instance + 0x1F0, script_context_ptr) && script_context_ptr != 0)
                {
                    script_context_instance_ = script_context_ptr;

                    std::uintptr_t resolved_lua_state = 0;
                    if (attachment_->read(script_context_ptr + thread_offsets::script_context_lua_state, resolved_lua_state))
                    {
                        return resolved_lua_state;
                    }
                }
            }
        }

        // Fallback: Resolve through static memory descriptor or TSS data
        std::uintptr_t tss_func = attachment_->rebase_remote(rva::get_tss_data);
        if (tss_func != 0)
        {
            std::uintptr_t tss_val = 0;
            attachment_->read(tss_func, tss_val);
            return tss_val;
        }

        return 0;
    }

    bool LuaVmBridge::elevate_thread_identity(std::uintptr_t thread_state, std::uint32_t identity, std::uint64_t capabilities)
    {
        if (!attachment_ || thread_state == 0)
            return false;

        // Luau thread identity and capabilities are stored in thread's ExtraSpace (offset preceding or within context)
        // Set identity level (level 8 for elevated execution)
        const std::uintptr_t identity_addr = thread_state + thread_offsets::lua_extraspace_identity;
        if (!attachment_->write(identity_addr, identity))
            return false;

        // Set capability bitmask (0x3FFFFFF)
        const std::uintptr_t caps_addr = thread_state + thread_offsets::lua_extraspace_caps;
        if (!attachment_->write(caps_addr, capabilities))
            return false;

        // Synchronize with tls_identity_struct anchor
        const std::uintptr_t tls_anchor = attachment_->rebase_remote(rva::tls_identity_struct);
        if (tls_anchor != 0)
        {
            attachment_->write(tls_anchor, capabilities);
        }

        return true;
    }

    std::optional<std::uintptr_t> LuaVmBridge::load_script(std::string_view source_or_bytecode, std::string_view chunk_name)
    {
        if (!attachment_ || !memory_bridge_)
            return std::nullopt;

        // Stage script payload into allocated environment memory
        if (!memory_bridge_->stage_script_payload(source_or_bytecode))
            return std::nullopt;

        // Resolve global state or thread instance
        if (global_lua_state_ == 0)
            global_lua_state_ = resolve_global_state();

        // Return staged buffer address ready for VM execution
        return memory_bridge_->bridge_buffer_address();
    }

    bool LuaVmBridge::dispatch_execution(std::uintptr_t thread_state)
    {
        if (!attachment_ || !attachment_->is_attached())
            return false;

        // Resolve task_defer dispatcher entry point
        const std::uintptr_t task_defer_addr = attachment_->rebase_remote(rva::task_defer);
        if (task_defer_addr == 0)
            return false;

        // If thread state is not elevated yet, elevate prior to scheduling
        elevate_thread_identity(thread_state);

        // Schedule thread invocation via ScriptContext resumption vector
        const std::uintptr_t resume_dispatcher = attachment_->rebase_remote(rva::script_context_resume);
        if (resume_dispatcher == 0)
            return false;

        // Update bridge descriptor status code
        std::uint32_t active_status = 0x200; // STATUS_DISPATCH_ENQUEUED
        attachment_->write(memory_bridge_->bridge_buffer_address() + offsetof(memory::BridgeContextDescriptor, status_code), active_status);

        return true;
    }

    bool LuaVmBridge::execute(std::string_view script_source)
    {
        if (!initialized_)
        {
            if (!initialize())
                return false;
        }

        auto staged_thread = load_script(script_source);
        if (!staged_thread.has_value())
            return false;

        return dispatch_execution(staged_thread.value());
    }

    void LuaVmBridge::configure_engine_flags()
    {
        if (!attachment_ || !attachment_->is_attached())
            return;

        // Enable fast pcall optimization
        const std::uintptr_t fast_pcall_flag = attachment_->rebase_remote(rva_flag::LuauFastpcall);
        if (fast_pcall_flag != 0)
        {
            const std::uint8_t enable_val = 1;
            attachment_->write(fast_pcall_flag, enable_val);
        }

        // Enable CI proto optimization
        const std::uintptr_t ci_proto_flag = attachment_->rebase_remote(rva_flag::LuauCIProto);
        if (ci_proto_flag != 0)
        {
            const std::uint8_t enable_val = 1;
            attachment_->write(ci_proto_flag, enable_val);
        }
    }
}
