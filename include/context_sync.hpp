#pragma once

#include <windows.h>
#include <cstdint>
#include <atomic>
#include <string_view>
#include <vector>
#include <span>
#include <memory>
#include <string>

namespace project_alpha::context
{
    /**
     * @brief Command operation identifiers for host-module synchronization
     */
    enum class SyncCommandOp : std::uint32_t
    {
        None = 0,
        Ping = 1,
        ResolveHostAnchors = 2,
        SynchronizeScheduler = 3,
        ExecutePayload = 4,
        ElevateCapabilities = 5,
        Terminate = 0xFFFFFFFF
    };

    /**
     * @brief Command packet transferred across the shared Process-Context ring buffer
     */
    struct alignas(16) SyncCommandPacket
    {
        std::uint32_t sequence_id{ 0 };
        SyncCommandOp opcode{ SyncCommandOp::None };
        std::uint32_t status_code{ 0 }; // 0 = Pending, 1 = Success, 2 = Failed
        std::uint32_t payload_size{ 0 };
        std::uintptr_t target_rva{ 0 };
        std::uintptr_t argument_block{ 0 };
        std::uint64_t parameter_mask{ 0 };
        char data_buffer[512]{ 0 };
    };

    /**
     * @brief Shared Memory Control Block residing in the target Process-Context
     */
    struct alignas(64) SharedContextSyncBlock
    {
        std::uint64_t magic{ 0x50524F4A5F414C50 }; // "PROJ_ALP"
        std::uint32_t version{ 0x00010000 };
        std::atomic<std::uint32_t> lock_flag{ 0 };

        // Host Context Anchors
        std::uintptr_t host_base_address{ 0 };
        std::uintptr_t task_scheduler_instance{ 0 };
        std::uintptr_t script_context_instance{ 0 };
        std::uintptr_t execution_thread_id{ 0 };

        // Mapped Module Anchors
        std::uintptr_t mapped_module_base{ 0 };
        std::uintptr_t mapped_module_entry{ 0 };

        // Communication Channels
        SyncCommandPacket inbound_packet{};
        SyncCommandPacket outbound_packet{};

        std::uint64_t heartbeat_counter{ 0 };
        std::uint32_t context_sync_state{ 0 }; // 0 = Unlinked, 1 = Staged, 2 = Synchronized
    };

    /**
     * @brief Telemetry snapshot from the Context-Sync link
     */
    struct ContextSyncTelemetry
    {
        std::uintptr_t shared_block_address{ 0 };
        std::uintptr_t host_task_scheduler{ 0 };
        std::uintptr_t host_script_context{ 0 };
        std::uint32_t commands_processed{ 0 };
        bool scheduler_synchronized{ false };
        bool context_link_active{ false };
    };

    /**
     * @brief Module B: Process-Context Synchronization Engine
     * Facilitates real-time, bidirectional communication between the mapped 
     * 64-bit module and the host runtime execution environment.
     */
    class ContextSyncEngine
    {
    public:
        explicit ContextSyncEngine(HANDLE process_handle, std::uintptr_t host_base);
        ~ContextSyncEngine();

        ContextSyncEngine(const ContextSyncEngine&) = delete;
        ContextSyncEngine& operator=(const ContextSyncEngine&) = delete;

        ContextSyncEngine(ContextSyncEngine&&) noexcept;
        ContextSyncEngine& operator=(ContextSyncEngine&&) noexcept;

        /**
         * @brief Allocates and initializes the shared Process-Context control block in target space.
         * @param mapped_module_base Remote base of the module mapped by Module A.
         * @return Address of the remote SharedContextSyncBlock, or 0 on failure.
         */
        std::uintptr_t initialize_context_link(std::uintptr_t mapped_module_base);

        /**
         * @brief Resolves core host execution anchors (TaskScheduler, ScriptContext).
         */
        bool resolve_host_anchors(std::uintptr_t task_scheduler_rva, std::uintptr_t script_context_rva);

        /**
         * @brief Synchronizes the module's execution loop with host scheduler ticks.
         */
        bool synchronize_scheduler_cycle();

        /**
         * @brief Dispatches a synchronous command packet to the mapped module context.
         */
        bool dispatch_command(SyncCommandOp opcode, std::string_view payload, std::uintptr_t target_rva = 0);

        /**
         * @brief Queries heartbeat and verifies ongoing context communication.
         */
        [[nodiscard]] bool check_context_heartbeat();

        [[nodiscard]] const ContextSyncTelemetry& telemetry() const noexcept { return telemetry_; }
        [[nodiscard]] std::uintptr_t sync_block_address() const noexcept { return remote_sync_block_; }

    private:
        bool write_sync_block(const SharedContextSyncBlock& block);
        bool read_sync_block(SharedContextSyncBlock& block) const;

        HANDLE process_handle_{ nullptr };
        std::uintptr_t host_base_{ 0 };
        std::uintptr_t remote_sync_block_{ 0 };
        std::size_t sync_block_size_{ sizeof(SharedContextSyncBlock) };
        ContextSyncTelemetry telemetry_{};
    };
}
