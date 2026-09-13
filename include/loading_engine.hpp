#pragma once

#include <windows.h>
#include <cstdint>
#include <span>
#include <vector>
#include <string_view>
#include <memory>
#include <string>

namespace project_alpha::engine
{
    /**
     * @brief Information on a mapped PE section
     */
    struct MappedSectionInfo
    {
        std::string name;
        std::uintptr_t virtual_address{ 0 };
        std::size_t virtual_size{ 0 };
        std::size_t raw_size{ 0 };
        DWORD characteristics{ 0 };
        DWORD protection{ 0 };
    };

    /**
     * @brief Telemetry report generated after mapping
     */
    struct MappingTelemetry
    {
        std::uintptr_t target_base{ 0 };
        std::size_t image_size{ 0 };
        std::uintptr_t entrypoint_address{ 0 };
        std::intptr_t relocation_delta{ 0 };
        std::size_t sections_committed{ 0 };
        std::size_t imports_resolved{ 0 };
        bool execution_dispatched{ false };
    };

    /**
     * @brief 64-bit Dynamic Module-Mapping & PE Relocation Engine
     */
    class LoadingEngine
    {
    public:
        explicit LoadingEngine(HANDLE target_process_handle);
        ~LoadingEngine() = default;

        LoadingEngine(const LoadingEngine&) = delete;
        LoadingEngine& operator=(const LoadingEngine&) = delete;

        LoadingEngine(LoadingEngine&&) noexcept = default;
        LoadingEngine& operator=(LoadingEngine&&) noexcept = default;

        /**
         * @brief Maps a raw 64-bit binary buffer into the target process address space.
         * @param raw_image Span containing binary payload.
         * @param execute_entrypoint Whether to dispatch a context thread at the entrypoint.
         * @return Remote base address of mapped module, or 0 on failure.
         */
        std::uintptr_t map_module(std::span<const std::uint8_t> raw_image, bool execute_entrypoint = false);

        /**
         * @brief Reads a 64-bit module from disk and maps it into target space.
         */
        std::uintptr_t map_module_from_file(std::wstring_view file_path, bool execute_entrypoint = false);

        [[nodiscard]] const MappingTelemetry& telemetry() const noexcept { return telemetry_; }
        [[nodiscard]] const std::vector<MappedSectionInfo>& mapped_sections() const noexcept { return sections_; }
        [[nodiscard]] std::uintptr_t remote_base() const noexcept { return telemetry_.target_base; }

    private:
        [[nodiscard]] bool validate_pe64(std::span<const std::uint8_t> buffer) const noexcept;
        
        bool commit_headers_and_sections(
            std::span<const std::uint8_t> buffer,
            std::uintptr_t remote_base,
            const IMAGE_NT_HEADERS64* nt_headers);

        bool apply_relocation_logic(
            std::span<const std::uint8_t> buffer,
            std::uintptr_t remote_base,
            std::intptr_t delta,
            const IMAGE_NT_HEADERS64* nt_headers);

        bool resolve_import_address_table(
            std::span<const std::uint8_t> buffer,
            std::uintptr_t remote_base,
            const IMAGE_NT_HEADERS64* nt_headers);

        bool apply_memory_integrity_protections(
            std::uintptr_t remote_base,
            const IMAGE_NT_HEADERS64* nt_headers);

        bool dispatch_context_thread(std::uintptr_t entrypoint_address, std::uintptr_t remote_base);

        static DWORD resolve_page_protection(DWORD characteristics) noexcept;

        HANDLE process_handle_{ nullptr };
        MappingTelemetry telemetry_{};
        std::vector<MappedSectionInfo> sections_{};
    };
}
