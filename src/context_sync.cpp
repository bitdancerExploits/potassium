#include "context_sync.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>

namespace project_alpha::context
{
    ContextSyncEngine::ContextSyncEngine(HANDLE process_handle, std::uintptr_t host_base)
        : process_handle_(process_handle), host_base_(host_base)
    {
    }

    ContextSyncEngine::~ContextSyncEngine()
    {
        if (remote_sync_block_ && process_handle_)
        {
            VirtualFreeEx(process_handle_, reinterpret_cast<void*>(remote_sync_block_), 0, MEM_RELEASE);
            remote_sync_block_ = 0;
        }
    }

    ContextSyncEngine::ContextSyncEngine(ContextSyncEngine&& other) noexcept
        : process_handle_(other.process_handle_)
        , host_base_(other.host_base_)
        , remote_sync_block_(other.remote_sync_block_)
        , sync_block_size_(other.sync_block_size_)
        , telemetry_(other.telemetry_)
    {
        other.remote_sync_block_ = 0;
        other.process_handle_ = nullptr;
    }

    ContextSyncEngine& ContextSyncEngine::operator=(ContextSyncEngine&& other) noexcept
    {
        if (this != &other)
        {
            if (remote_sync_block_ && process_handle_)
            {
                VirtualFreeEx(process_handle_, reinterpret_cast<void*>(remote_sync_block_), 0, MEM_RELEASE);
            }

            process_handle_ = other.process_handle_;
            host_base_ = other.host_base_;
            remote_sync_block_ = other.remote_sync_block_;
            sync_block_size_ = other.sync_block_size_;
            telemetry_ = other.telemetry_;

            other.remote_sync_block_ = 0;
            other.process_handle_ = nullptr;
        }
        return *this;
    }

    std::uintptr_t ContextSyncEngine::initialize_context_link(std::uintptr_t mapped_module_base)
    {
        if (!process_handle_)
        {
            std::cerr << "[-] [Module-B] Process handle invalid. Context-Sync initialization aborted.\n";
            return 0;
        }

        // Allocate Shared Control Block in Target Process Context
        void* alloc_ptr = VirtualAllocEx(
            process_handle_,
            nullptr,
            sync_block_size_,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );

        if (!alloc_ptr)
        {
            std::cerr << "[-] [Module-B] Allocation of Context-Sync control block failed. Error: " 
                      << GetLastError() << "\n";
            return 0;
        }

        remote_sync_block_ = reinterpret_cast<std::uintptr_t>(alloc_ptr);

        SharedContextSyncBlock local_block{};
        local_block.magic = 0x50524F4A5F414C50;
        local_block.version = 0x00010000;
        local_block.lock_flag = 0;
        local_block.host_base_address = host_base_;
        local_block.mapped_module_base = mapped_module_base;
        local_block.context_sync_state = 1; // Staged

        if (!write_sync_block(local_block))
        {
            std::cerr << "[-] [Module-B] Failed initializing SharedContextSyncBlock payload.\n";
            VirtualFreeEx(process_handle_, alloc_ptr, 0, MEM_RELEASE);
            remote_sync_block_ = 0;
            return 0;
        }

        telemetry_.shared_block_address = remote_sync_block_;
        telemetry_.context_link_active = true;

        std::cout << "[+] [Module-B] Context-Sync Channel Established at 0x" 
                  << std::hex << remote_sync_block_ << "\n";
        return remote_sync_block_;
    }

    bool ContextSyncEngine::resolve_host_anchors(std::uintptr_t task_scheduler_rva, std::uintptr_t script_context_rva)
    {
        if (!remote_sync_block_)
            return false;

        SharedContextSyncBlock block{};
        if (!read_sync_block(block))
            return false;

        // Resolve TaskScheduler anchor pointer
        if (task_scheduler_rva != 0)
        {
            const std::uintptr_t scheduler_target = host_base_ + task_scheduler_rva;
            std::uintptr_t resolved_ptr = 0;
            ReadProcessMemory(process_handle_, reinterpret_cast<void*>(scheduler_target), 
                              &resolved_ptr, sizeof(resolved_ptr), nullptr);
            block.task_scheduler_instance = resolved_ptr;
            telemetry_.host_task_scheduler = resolved_ptr;
        }

        // Resolve ScriptContext anchor pointer
        if (script_context_rva != 0)
        {
            const std::uintptr_t context_target = host_base_ + script_context_rva;
            std::uintptr_t resolved_ctx = 0;
            ReadProcessMemory(process_handle_, reinterpret_cast<void*>(context_target),
                              &resolved_ctx, sizeof(resolved_ctx), nullptr);
            block.script_context_instance = resolved_ctx;
            telemetry_.host_script_context = resolved_ctx;
        }

        return write_sync_block(block);
    }

    bool ContextSyncEngine::synchronize_scheduler_cycle()
    {
        if (!remote_sync_block_)
            return false;

        SharedContextSyncBlock block{};
        if (!read_sync_block(block))
            return false;

        block.context_sync_state = 2; // Synchronized
        block.heartbeat_counter++;
        telemetry_.scheduler_synchronized = true;

        std::cout << "[+] [Module-B] Context-Sync State: SYNCHRONIZED with Host Engine.\n";
        return write_sync_block(block);
    }

    bool ContextSyncEngine::dispatch_command(SyncCommandOp opcode, std::string_view payload, std::uintptr_t target_rva)
    {
        if (!remote_sync_block_)
            return false;

        SharedContextSyncBlock block{};
        if (!read_sync_block(block))
            return false;

        static std::uint32_t seq = 1;
        block.inbound_packet.sequence_id = seq++;
        block.inbound_packet.opcode = opcode;
        block.inbound_packet.status_code = 0; // Dispatched / Pending
        block.inbound_packet.target_rva = target_rva;
        block.inbound_packet.payload_size = static_cast<std::uint32_t>(
            std::min<std::size_t>(payload.size(), sizeof(block.inbound_packet.data_buffer) - 1));

        std::memset(block.inbound_packet.data_buffer, 0, sizeof(block.inbound_packet.data_buffer));
        if (block.inbound_packet.payload_size > 0)
        {
            std::memcpy(block.inbound_packet.data_buffer, payload.data(), block.inbound_packet.payload_size);
        }

        block.heartbeat_counter++;
        telemetry_.commands_processed++;

        return write_sync_block(block);
    }

    bool ContextSyncEngine::check_context_heartbeat()
    {
        if (!remote_sync_block_)
            return false;

        SharedContextSyncBlock block{};
        if (!read_sync_block(block))
            return false;

        return (block.magic == 0x50524F4A5F414C50) && (block.context_sync_state >= 1);
    }

    bool ContextSyncEngine::write_sync_block(const SharedContextSyncBlock& block)
    {
        SIZE_T written = 0;
        return WriteProcessMemory(
            process_handle_,
            reinterpret_cast<void*>(remote_sync_block_),
            &block,
            sizeof(block),
            &written
        ) && (written == sizeof(block));
    }

    bool ContextSyncEngine::read_sync_block(SharedContextSyncBlock& block) const
    {
        SIZE_T read = 0;
        return ReadProcessMemory(
            process_handle_,
            reinterpret_cast<void*>(remote_sync_block_),
            &block,
            sizeof(block),
            &read
        ) && (read == sizeof(block));
    }
}
