#pragma once

#include "offsets.hpp"
#include "memory_bridge.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <optional>

namespace project_x::vm
{
    /**
     * @brief Luau Internal Object Value Type Enums
     */
    enum class LuaType : std::int32_t
    {
        Nil = 0,
        Boolean = 1,
        LightUserData = 2,
        Number = 3,
        Vector = 4,
        String = 5,
        Table = 6,
        Function = 7,
        UserData = 8,
        Thread = 9,
        Buffer = 10
    };

    /**
     * @brief Thread Identity Security Flags & Capabilities
     */
    struct ThreadSecurityContext
    {
        std::uint64_t capabilities{ 0x3FFFFFF }; // Full permissions mask
        std::uint32_t identity_level{ 8 };       // Elevated execution level
        std::uintptr_t script_ptr{ 0 };
    };

    /**
     * @brief Engine TaskScheduler Job Descriptor
     */
    struct TaskSchedulerJob
    {
        std::uintptr_t vftable{ 0 };
        std::uintptr_t name_ptr{ 0 };
        std::string name{};
        double step_rate{ 0.0 };
        std::uintptr_t instance{ 0 };
    };

    /**
     * @brief Execution Job Hook Interface
     */
    using ExecutionCallback = std::function<void(std::uintptr_t thread_state)>;

    /**
     * @brief Module 2: The Lua VM & Thread Execution Bridge
     * High-speed link to orchestrate VM compilation, thread state resolution, 
     * identity elevation, and TaskScheduler-synchronized execution.
     */
    class LuaVmBridge
    {
    public:
        explicit LuaVmBridge(std::shared_ptr<memory::ProcessAttachment> attachment,
                             std::shared_ptr<memory::EnvironmentBridge> memory_bridge);
        ~LuaVmBridge();

        LuaVmBridge(const LuaVmBridge&) = delete;
        LuaVmBridge& operator=(const LuaVmBridge&) = delete;

        /**
         * @brief Initializes the VM bridge, verifies opcode tables, and configures engine flags.
         */
        bool initialize();

        /**
         * @brief Queries and resolves the active TaskScheduler instance pointer.
         */
        [[nodiscard]] std::uintptr_t get_task_scheduler_instance() const;

        /**
         * @brief Inspects registered jobs in the TaskScheduler job cycle.
         */
        [[nodiscard]] std::vector<TaskSchedulerJob> enumerate_scheduler_jobs() const;

        /**
         * @brief Resolves the main engine ScriptContext and root lua_State pointer.
         */
        [[nodiscard]] std::uintptr_t resolve_global_state();

        /**
         * @brief Elevates the thread's security context and capabilities to Level 8 / 0x3FFFFFF.
         */
        bool elevate_thread_identity(std::uintptr_t thread_state, std::uint32_t identity = 8, std::uint64_t capabilities = 0x3FFFFFF);

        /**
         * @brief Compiles and loads Luau bytecode or source into the target VM.
         */
        [[nodiscard]] std::optional<std::uintptr_t> load_script(std::string_view source_or_bytecode, std::string_view chunk_name = "@ProjectX");

        /**
         * @brief Dispatches the loaded script thread synchronously onto the engine's TaskScheduler.
         */
        bool dispatch_execution(std::uintptr_t thread_state);

        /**
         * @brief High-level execution entry: stages source into memory buffer and enqueues task_defer.
         */
        bool execute(std::string_view script_source);

        /**
         * @brief Enables or optimizes engine Luau execution flags.
         */
        void configure_engine_flags();

        [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }
        [[nodiscard]] std::uintptr_t global_lua_state() const noexcept { return global_lua_state_; }

    private:
        std::shared_ptr<memory::ProcessAttachment> attachment_;
        std::shared_ptr<memory::EnvironmentBridge> memory_bridge_;
        std::uintptr_t task_scheduler_instance_{ 0 };
        std::uintptr_t global_lua_state_{ 0 };
        std::uintptr_t script_context_instance_{ 0 };
        bool initialized_{ false };
    };
}
