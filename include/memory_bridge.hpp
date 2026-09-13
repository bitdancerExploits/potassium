#pragma once

#include "offsets.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <system_error>
#include <span>
#include <tlhelp32.h>

namespace project_x::memory
{
    /**
     * @brief Memory Protection Guard (RAII)
     * Automatically restores the previous memory protection state on destruction.
     */
    class PageProtectionGuard
    {
    public:
        PageProtectionGuard(HANDLE process_handle, void* address, std::size_t size, DWORD new_protection);
        ~PageProtectionGuard();

        PageProtectionGuard(const PageProtectionGuard&) = delete;
        PageProtectionGuard& operator=(const PageProtectionGuard&) = delete;

        PageProtectionGuard(PageProtectionGuard&& other) noexcept;
        PageProtectionGuard& operator=(PageProtectionGuard&& other) noexcept;

        [[nodiscard]] bool is_valid() const noexcept { return succeeded_; }
        [[nodiscard]] DWORD old_protection() const noexcept { return old_protection_; }

    private:
        HANDLE process_handle_{ nullptr };
        void* address_{ nullptr };
        std::size_t size_{ 0 };
        DWORD old_protection_{ 0 };
        bool succeeded_{ false };
    };

    /**
     * @brief RAII Virtual Memory Block
     * Manages allocated memory pages within the target process space.
     */
    class VirtualMemoryRegion
    {
    public:
        VirtualMemoryRegion() = default;
        VirtualMemoryRegion(HANDLE process_handle, std::size_t size, DWORD allocation_type, DWORD protection);
        ~VirtualMemoryRegion();

        VirtualMemoryRegion(const VirtualMemoryRegion&) = delete;
        VirtualMemoryRegion& operator=(const VirtualMemoryRegion&) = delete;

        VirtualMemoryRegion(VirtualMemoryRegion&& other) noexcept;
        VirtualMemoryRegion& operator=(VirtualMemoryRegion&& other) noexcept;

        [[nodiscard]] void* get() const noexcept { return base_address_; }
        [[nodiscard]] std::uintptr_t as_uintptr() const noexcept { return reinterpret_cast<std::uintptr_t>(base_address_); }
        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] bool is_allocated() const noexcept { return base_address_ != nullptr; }

        bool write(std::size_t offset, const void* buffer, std::size_t size);
        bool read(std::size_t offset, void* buffer, std::size_t size) const;

        void release() noexcept;

    private:
        HANDLE process_handle_{ nullptr };
        void* base_address_{ nullptr };
        std::size_t size_{ 0 };
    };

    /**
     * @brief Process Attachment & Target State Inspector
     * Handles process discovery, privilege elevation, process handles, and module bases.
     */
    class ProcessAttachment
    {
    public:
        ProcessAttachment() = default;
        ~ProcessAttachment();

        ProcessAttachment(const ProcessAttachment&) = delete;
        ProcessAttachment& operator=(const ProcessAttachment&) = delete;

        /**
         * @brief Elevates the current thread token with SeDebugPrivilege.
         */
        static bool elevate_debug_privilege() noexcept;

        /**
         * @brief Attach to an external process by executable name.
         */
        bool attach_by_name(std::wstring_view process_name, DWORD access_rights = PROCESS_ALL_ACCESS);

        /**
         * @brief Attach to an external process by process ID.
         */
        bool attach_by_pid(DWORD pid, DWORD access_rights = PROCESS_ALL_ACCESS);

        /**
         * @brief Attach using current in-process context.
         */
        bool attach_in_process();

        void detach() noexcept;

        [[nodiscard]] bool is_attached() const noexcept { return attached_; }
        [[nodiscard]] bool is_in_process() const noexcept { return in_process_; }
        [[nodiscard]] HANDLE process_handle() const noexcept { return process_handle_; }
        [[nodiscard]] DWORD process_id() const noexcept { return process_id_; }
        [[nodiscard]] std::uintptr_t target_base() const noexcept { return target_base_; }

        [[nodiscard]] std::uintptr_t rebase_remote(std::uintptr_t rva) const noexcept
        {
            return rva ? target_base_ + rva : 0;
        }

        template <typename T>
        bool read(std::uintptr_t address, T& out_value) const
        {
            return read_raw(address, &out_value, sizeof(T));
        }

        template <typename T>
        bool write(std::uintptr_t address, const T& value)
        {
            return write_raw(address, &value, sizeof(T));
        }

        bool read_raw(std::uintptr_t address, void* buffer, std::size_t size) const;
        bool write_raw(std::uintptr_t address, const void* buffer, std::size_t size);

    private:
        bool find_module_base(std::wstring_view module_name);

        HANDLE process_handle_{ nullptr };
        DWORD process_id_{ 0 };
        std::uintptr_t target_base_{ 0 };
        bool attached_{ false };
        bool in_process_{ false };
    };

    /**
     * @brief Runtime Execution & Environment Bridge State
     * Communicates between the host execution environment and the engine's internal Lua state.
     */
    struct BridgeContextDescriptor
    {
        std::uintptr_t task_scheduler_addr{ 0 };
        std::uintptr_t script_context_addr{ 0 };
        std::uintptr_t lua_state_addr{ 0 };
        std::uintptr_t identity_struct_addr{ 0 };
        std::uint64_t capabilities{ 0x3FFFFFF };
        std::uint32_t execution_flags{ 0 };
        std::uint32_t status_code{ 0 };
    };

    class EnvironmentBridge
    {
    public:
        explicit EnvironmentBridge(std::shared_ptr<ProcessAttachment> attachment);
        ~EnvironmentBridge();

        /**
         * @brief Initializes the Environment Bridge link.
         * Verifies target memory access, resolves core RVA anchors, and stages the execution bridge buffer.
         */
        bool initialize();

        /**
         * @brief Allocates an isolated environment buffer for script payload staging and execution frames.
         */
        bool allocate_bridge_buffers(std::size_t script_buffer_size = 0x10000);

        /**
         * @brief Verifies whether critical engine anchors are resolved and valid.
         */
        [[nodiscard]] bool verify_engine_anchors() const;

        /**
         * @brief Injects or stages a script payload into the runtime execution buffer.
         */
        bool stage_script_payload(std::string_view bytecode_or_source);

        [[nodiscard]] const BridgeContextDescriptor& context() const noexcept { return context_descriptor_; }
        [[nodiscard]] std::uintptr_t bridge_buffer_address() const noexcept { return bridge_buffer_.as_uintptr(); }

    private:
        std::shared_ptr<ProcessAttachment> attachment_;
        VirtualMemoryRegion bridge_buffer_;
        VirtualMemoryRegion script_payload_buffer_;
        BridgeContextDescriptor context_descriptor_{};
        bool initialized_{ false };
    };
}
