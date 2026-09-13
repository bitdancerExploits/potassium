#include "loading_engine.hpp"
#include <fstream>
#include <iostream>

namespace project_alpha::engine
{
    struct BaseRelocationEntry
    {
        std::uint16_t offset : 12;
        std::uint16_t type   : 4;
    };

    LoadingEngine::LoadingEngine(HANDLE target_process_handle)
        : process_handle_(target_process_handle)
    {
    }

    DWORD LoadingEngine::resolve_page_protection(DWORD characteristics) noexcept
    {
        const bool readable   = (characteristics & IMAGE_SCN_MEM_READ) != 0;
        const bool writeable  = (characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        const bool executable = (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;

        if (executable)
        {
            if (writeable)
                return PAGE_EXECUTE_READWRITE;
            if (readable)
                return PAGE_EXECUTE_READ;
            return PAGE_EXECUTE;
        }
        else
        {
            if (writeable)
                return PAGE_READWRITE;
            if (readable)
                return PAGE_READONLY;
            return PAGE_NOACCESS;
        }
    }

    bool LoadingEngine::validate_pe64(std::span<const std::uint8_t> buffer) const noexcept
    {
        if (buffer.size() < sizeof(IMAGE_DOS_HEADER))
            return false;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(buffer.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        if (dos->e_lfanew <= 0 || buffer.size() < static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64))
            return false;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(buffer.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;

        return (nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) &&
               (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    }

    std::uintptr_t LoadingEngine::map_module_from_file(std::wstring_view file_path, bool execute_entrypoint)
    {
        std::ifstream file(std::wstring(file_path), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            std::cerr << "[-] [Module-A] Failed to open binary source file.\n";
            return 0;
        }

        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
        {
            std::cerr << "[-] [Module-A] Failed reading binary stream.\n";
            return 0;
        }

        return map_module(buffer, execute_entrypoint);
    }

    std::uintptr_t LoadingEngine::map_module(std::span<const std::uint8_t> raw_image, bool execute_entrypoint)
    {
        telemetry_ = {};
        sections_.clear();

        if (!process_handle_)
        {
            std::cerr << "[-] [Module-A] Invalid target process context handle.\n";
            return 0;
        }

        if (!validate_pe64(raw_image))
        {
            std::cerr << "[-] [Module-A] PE64 format validation failed.\n";
            return 0;
        }

        const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw_image.data());
        const auto* nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            raw_image.data() + dos_header->e_lfanew);

        const std::size_t image_size = nt_headers->OptionalHeader.SizeOfImage;

        // 1. Allocate in target address-space
        void* remote_base = VirtualAllocEx(
            process_handle_,
            nullptr,
            image_size,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );

        if (!remote_base)
        {
            std::cerr << "[-] [Module-A] Target address-space allocation failed: " << GetLastError() << "\n";
            return 0;
        }

        const auto remote_base_addr = reinterpret_cast<std::uintptr_t>(remote_base);
        const std::intptr_t delta = remote_base_addr - nt_headers->OptionalHeader.ImageBase;

        telemetry_.target_base = remote_base_addr;
        telemetry_.image_size = image_size;
        telemetry_.relocation_delta = delta;

        // 2. Commit PE Headers and Sections
        if (!commit_headers_and_sections(raw_image, remote_base_addr, nt_headers))
        {
            VirtualFreeEx(process_handle_, remote_base, 0, MEM_RELEASE);
            return 0;
        }

        // 3. Relocation-Logic
        if (delta != 0)
        {
            if (!apply_relocation_logic(raw_image, remote_base_addr, delta, nt_headers))
            {
                VirtualFreeEx(process_handle_, remote_base, 0, MEM_RELEASE);
                return 0;
            }
        }

        // 4. Resolve Import Address Table
        if (!resolve_import_address_table(raw_image, remote_base_addr, nt_headers))
        {
            VirtualFreeEx(process_handle_, remote_base, 0, MEM_RELEASE);
            return 0;
        }

        // 5. Memory-Integrity Protection Restoration
        if (!apply_memory_integrity_protections(remote_base_addr, nt_headers))
        {
            VirtualFreeEx(process_handle_, remote_base, 0, MEM_RELEASE);
            return 0;
        }

        // 6. Entrypoint Dispatch & Context Sync
        if (nt_headers->OptionalHeader.AddressOfEntryPoint != 0)
        {
            telemetry_.entrypoint_address = remote_base_addr + nt_headers->OptionalHeader.AddressOfEntryPoint;
            if (execute_entrypoint)
            {
                telemetry_.execution_dispatched = dispatch_context_thread(
                    telemetry_.entrypoint_address, remote_base_addr);
            }
        }

        return remote_base_addr;
    }

    bool LoadingEngine::commit_headers_and_sections(
        std::span<const std::uint8_t> buffer,
        std::uintptr_t remote_base,
        const IMAGE_NT_HEADERS64* nt_headers)
    {
        SIZE_T written = 0;
        if (!WriteProcessMemory(
                process_handle_,
                reinterpret_cast<void*>(remote_base),
                buffer.data(),
                nt_headers->OptionalHeader.SizeOfHeaders,
                &written))
        {
            return false;
        }

        const auto* section = IMAGE_FIRST_SECTION(nt_headers);
        for (WORD i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i, ++section)
        {
            MappedSectionInfo info{};
            char sec_name[9] = { 0 };
            std::memcpy(sec_name, section->Name, 8);
            info.name = sec_name;
            info.virtual_address = remote_base + section->VirtualAddress;
            info.virtual_size = section->Misc.VirtualSize;
            info.raw_size = section->SizeOfRawData;
            info.characteristics = section->Characteristics;
            info.protection = resolve_page_protection(section->Characteristics);
            sections_.push_back(info);

            if (section->SizeOfRawData == 0)
                continue;

            const void* src = buffer.data() + section->PointerToRawData;
            void* dst = reinterpret_cast<void*>(remote_base + section->VirtualAddress);

            if (!WriteProcessMemory(process_handle_, dst, src, section->SizeOfRawData, &written))
            {
                return false;
            }
            telemetry_.sections_committed++;
        }

        return true;
    }

    bool LoadingEngine::apply_relocation_logic(
        std::span<const std::uint8_t> buffer,
        std::uintptr_t remote_base,
        std::intptr_t delta,
        const IMAGE_NT_HEADERS64* nt_headers)
    {
        const auto& reloc_dir = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (reloc_dir.Size == 0 || reloc_dir.VirtualAddress == 0)
            return true;

        const auto* reloc_block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(
            buffer.data() + reloc_dir.VirtualAddress);
        const auto* reloc_end = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(
            reinterpret_cast<const std::uint8_t*>(reloc_block) + reloc_dir.Size);

        while (reloc_block < reloc_end && reloc_block->SizeOfBlock > 0)
        {
            const std::size_t entry_count = 
                (reloc_block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(BaseRelocationEntry);
            const auto* entries = reinterpret_cast<const BaseRelocationEntry*>(reloc_block + 1);

            for (std::size_t i = 0; i < entry_count; ++i)
            {
                if (entries[i].type == IMAGE_REL_BASED_DIR64)
                {
                    const std::uintptr_t patch_address = 
                        remote_base + reloc_block->VirtualAddress + entries[i].offset;

                    std::uint64_t target_val = 0;
                    if (ReadProcessMemory(process_handle_, reinterpret_cast<void*>(patch_address), &target_val, sizeof(target_val), nullptr))
                    {
                        target_val += delta;
                        WriteProcessMemory(process_handle_, reinterpret_cast<void*>(patch_address), &target_val, sizeof(target_val), nullptr);
                    }
                }
            }

            reloc_block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(
                reinterpret_cast<const std::uint8_t*>(reloc_block) + reloc_block->SizeOfBlock);
        }

        return true;
    }

    bool LoadingEngine::resolve_import_address_table(
        std::span<const std::uint8_t> buffer,
        std::uintptr_t remote_base,
        const IMAGE_NT_HEADERS64* nt_headers)
    {
        const auto& import_dir = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (import_dir.Size == 0 || import_dir.VirtualAddress == 0)
            return true;

        const auto* import_desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
            buffer.data() + import_dir.VirtualAddress);

        while (import_desc->Name != 0)
        {
            const char* module_name = reinterpret_cast<const char*>(buffer.data() + import_desc->Name);
            HMODULE host_mod = LoadLibraryA(module_name);
            if (!host_mod)
            {
                std::cerr << "[-] [Module-A] Failed loading host module dependency: " << module_name << "\n";
                return false;
            }

            const std::uintptr_t thunk_rva = import_desc->OriginalFirstThunk 
                ? import_desc->OriginalFirstThunk 
                : import_desc->FirstThunk;

            const auto* thunk_data = reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                buffer.data() + thunk_rva);
            std::uintptr_t iat_write_ptr = remote_base + import_desc->FirstThunk;

            while (thunk_data->u1.AddressOfData != 0)
            {
                std::uint64_t resolved_fn = 0;

                if (IMAGE_SNAP_BY_ORDINAL64(thunk_data->u1.Ordinal))
                {
                    const auto ord = reinterpret_cast<LPCSTR>(IMAGE_ORDINAL64(thunk_data->u1.Ordinal));
                    resolved_fn = reinterpret_cast<std::uint64_t>(GetProcAddress(host_mod, ord));
                }
                else
                {
                    const auto* import_by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                        buffer.data() + thunk_data->u1.AddressOfData);
                    resolved_fn = reinterpret_cast<std::uint64_t>(
                        GetProcAddress(host_mod, import_by_name->Name));
                }

                if (!resolved_fn)
                {
                    std::cerr << "[-] [Module-A] Failed resolving import in: " << module_name << "\n";
                    return false;
                }

                WriteProcessMemory(
                    process_handle_,
                    reinterpret_cast<void*>(iat_write_ptr),
                    &resolved_fn,
                    sizeof(resolved_fn),
                    nullptr
                );

                iat_write_ptr += sizeof(std::uint64_t);
                telemetry_.imports_resolved++;
                ++thunk_data;
            }

            ++import_desc;
        }

        return true;
    }

    bool LoadingEngine::apply_memory_integrity_protections(
        std::uintptr_t remote_base,
        const IMAGE_NT_HEADERS64* nt_headers)
    {
        const auto* section = IMAGE_FIRST_SECTION(nt_headers);
        for (WORD i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i, ++section)
        {
            if (section->Misc.VirtualSize == 0)
                continue;

            void* sec_addr = reinterpret_cast<void*>(remote_base + section->VirtualAddress);
            const DWORD protection = resolve_page_protection(section->Characteristics);
            DWORD old_protection = 0;

            if (!VirtualProtectEx(
                    process_handle_,
                    sec_addr,
                    section->Misc.VirtualSize,
                    protection,
                    &old_protection))
            {
                return false;
            }
        }

        return true;
    }

    bool LoadingEngine::dispatch_context_thread(std::uintptr_t entrypoint_address, std::uintptr_t remote_base)
    {
        HANDLE thread = CreateRemoteThread(
            process_handle_,
            nullptr,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(entrypoint_address),
            reinterpret_cast<void*>(remote_base),
            0,
            nullptr
        );

        if (!thread)
            return false;

        WaitForSingleObject(thread, 3000);
        CloseHandle(thread);
        return true;
    }
}
