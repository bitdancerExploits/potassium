#include "memory_bridge.hpp"
#include <iostream>
#include <cstring>

namespace project_x::memory
{
    // =========================================================================
    // PageProtectionGuard Implementation
    // =========================================================================

    PageProtectionGuard::PageProtectionGuard(HANDLE process_handle, void* address, std::size_t size, DWORD new_protection)
        : process_handle_(process_handle), address_(address), size_(size)
    {
        if (!address_ || size_ == 0)
            return;

        if (process_handle_ == GetCurrentProcess() || process_handle_ == nullptr)
        {
            succeeded_ = ::VirtualProtect(address_, size_, new_protection, &old_protection_) != 0;
        }
        else
        {
            succeeded_ = ::VirtualProtectEx(process_handle_, address_, size_, new_protection, &old_protection_) != 0;
        }
    }

    PageProtectionGuard::~PageProtectionGuard()
    {
        if (succeeded_ && address_ && size_ > 0)
        {
            DWORD discard = 0;
            if (process_handle_ == GetCurrentProcess() || process_handle_ == nullptr)
            {
                ::VirtualProtect(address_, size_, old_protection_, &discard);
            }
            else
            {
                ::VirtualProtectEx(process_handle_, address_, size_, old_protection_, &discard);
            }
        }
    }

    PageProtectionGuard::PageProtectionGuard(PageProtectionGuard&& other) noexcept
        : process_handle_(other.process_handle_),
          address_(other.address_),
          size_(other.size_),
          old_protection_(other.old_protection_),
          succeeded_(other.succeeded_)
    {
        other.succeeded_ = false;
        other.address_ = nullptr;
        other.size_ = 0;
    }

    PageProtectionGuard& PageProtectionGuard::operator=(PageProtectionGuard&& other) noexcept
    {
        if (this != &other)
        {
            if (succeeded_ && address_ && size_ > 0)
            {
                DWORD discard = 0;
                if (process_handle_ == GetCurrentProcess() || process_handle_ == nullptr)
                    ::VirtualProtect(address_, size_, old_protection_, &discard);
                else
                    ::VirtualProtectEx(process_handle_, address_, size_, old_protection_, &discard);
            }

            process_handle_ = other.process_handle_;
            address_ = other.address_;
            size_ = other.size_;
            old_protection_ = other.old_protection_;
            succeeded_ = other.succeeded_;

            other.succeeded_ = false;
            other.address_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    // =========================================================================
    // VirtualMemoryRegion Implementation
    // =========================================================================

    VirtualMemoryRegion::VirtualMemoryRegion(HANDLE process_handle, std::size_t size, DWORD allocation_type, DWORD protection)
        : process_handle_(process_handle), size_(size)
    {
        if (size_ == 0)
            return;

        if (process_handle_ == GetCurrentProcess() || process_handle_ == nullptr)
        {
            base_address_ = ::VirtualAlloc(nullptr, size_, allocation_type, protection);
        }
        else
        {
            base_address_ = ::VirtualAllocEx(process_handle_, nullptr, size_, allocation_type, protection);
        }
    }

    VirtualMemoryRegion::~VirtualMemoryRegion()
    {
        release();
    }

    VirtualMemoryRegion::VirtualMemoryRegion(VirtualMemoryRegion&& other) noexcept
        : process_handle_(other.process_handle_),
          base_address_(other.base_address_),
          size_(other.size_)
    {
        other.base_address_ = nullptr;
        other.size_ = 0;
    }

    VirtualMemoryRegion& VirtualMemoryRegion::operator=(VirtualMemoryRegion&& other) noexcept
    {
        if (this != &other)
        {
            release();
            process_handle_ = other.process_handle_;
            base_address_ = other.base_address_;
            size_ = other.size_;

            other.base_address_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void VirtualMemoryRegion::release() noexcept
    {
        if (base_address_)
        {
            if (process_handle_ == GetCurrentProcess() || process_handle_ == nullptr)
            {
                ::VirtualFree(base_address_, 0, MEM_RELEASE);
            }
            else
            {
                ::VirtualFreeEx(process_handle_, base_address_, 0, MEM_RELEASE);
            }
            base_address_ = nullptr;
            size_ = 0;
        }
    }

    bool VirtualMemoryRegion::write(std::size_t offset, const void* buffer, std::size_t size)
    {
        if (!base_address_ || (offset + size) > size_ || !buffer)
            return false;

        auto* destination = static_cast<std::uint8_t*>(base_address_) + offset;

        if (process_handle_ == GetCurrentProcess() || process_handle_ == nullptr)
        {
            std::memcpy(destination, buffer, size);
            return true;
        }

        SIZE_T bytes_written = 0;
        return ::WriteProcessMemory(process_handle_, destination, buffer, size, &bytes_written) && (bytes_written == size);
    }

    bool VirtualMemoryRegion::read(std::size_t offset, void* buffer, std::size_t size) const
    {
        if (!base_address_ || (offset + size) > size_ || !buffer)
            return false;

        const auto* source = static_cast<const std::uint8_t*>(base_address_) + offset;

        if (process_handle_ == GetCurrentProcess() || process_handle_ == nullptr)
        {
            std::memcpy(buffer, source, size);
            return true;
        }

        SIZE_T bytes_read = 0;
        return ::ReadProcessMemory(process_handle_, source, buffer, size, &bytes_read) && (bytes_read == size);
    }

    // =========================================================================
    // ProcessAttachment Implementation
    // =========================================================================

    ProcessAttachment::~ProcessAttachment()
    {
        detach();
    }

    bool ProcessAttachment::elevate_debug_privilege() noexcept
    {
        HANDLE token_handle = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_handle))
            return false;

        LUID luid{};
        if (!::LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid))
        {
            ::CloseHandle(token_handle);
            return false;
        }

        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        ::AdjustTokenPrivileges(token_handle, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr);
        const bool result = (::GetLastError() == ERROR_SUCCESS);
        ::CloseHandle(token_handle);
        return result;
    }

    bool ProcessAttachment::attach_by_name(std::wstring_view process_name, DWORD access_rights)
    {
        detach();
        elevate_debug_privilege();

        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(PROCESSENTRY32W);

        DWORD found_pid = 0;
        if (::Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (process_name == entry.szExeFile)
                {
                    found_pid = entry.th32ProcessID;
                    break;
                }
            } while (::Process32NextW(snapshot, &entry));
        }

        ::CloseHandle(snapshot);

        if (found_pid == 0)
            return false;

        return attach_by_pid(found_pid, access_rights);
    }

    bool ProcessAttachment::attach_by_pid(DWORD pid, DWORD access_rights)
    {
        detach();
        elevate_debug_privilege();

        process_handle_ = ::OpenProcess(access_rights, FALSE, pid);
        if (!process_handle_)
            return false;

        process_id_ = pid;
        in_process_ = (pid == ::GetCurrentProcessId());

        if (!find_module_base(L""))
        {
            detach();
            return false;
        }

        attached_ = true;
        return true;
    }

    bool ProcessAttachment::attach_in_process()
    {
        detach();
        process_handle_ = ::GetCurrentProcess();
        process_id_ = ::GetCurrentProcessId();
        target_base_ = project_x::base();
        attached_ = true;
        in_process_ = true;
        return true;
    }

    void ProcessAttachment::detach() noexcept
    {
        if (process_handle_ && !in_process_)
        {
            ::CloseHandle(process_handle_);
        }
        process_handle_ = nullptr;
        process_id_ = 0;
        target_base_ = 0;
        attached_ = false;
        in_process_ = false;
    }

    bool ProcessAttachment::find_module_base(std::wstring_view module_name)
    {
        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id_);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(MODULEENTRY32W);

        bool resolved = false;
        if (::Module32FirstW(snapshot, &entry))
        {
            do
            {
                if (module_name.empty() || module_name == entry.szModule)
                {
                    target_base_ = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                    resolved = true;
                    break;
                }
            } while (::Module32NextW(snapshot, &entry));
        }

        ::CloseHandle(snapshot);
        return resolved;
    }

    bool ProcessAttachment::read_raw(std::uintptr_t address, void* buffer, std::size_t size) const
    {
        if (!attached_ || address == 0 || !buffer || size == 0)
            return false;

        SIZE_T bytes_read = 0;
        return ::ReadProcessMemory(process_handle_, reinterpret_cast<LPCVOID>(address), buffer, size, &bytes_read) && (bytes_read == size);
    }

    bool ProcessAttachment::write_raw(std::uintptr_t address, const void* buffer, std::size_t size)
    {
        if (!attached_ || address == 0 || !buffer || size == 0)
            return false;

        DWORD old_protect = 0;
        if (!::VirtualProtectEx(process_handle_, reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE, &old_protect))
            return false;

        SIZE_T bytes_written = 0;
        const BOOL write_ok = ::WriteProcessMemory(process_handle_, reinterpret_cast<LPVOID>(address), buffer, size, &bytes_written);

        DWORD discard = 0;
        ::VirtualProtectEx(process_handle_, reinterpret_cast<LPVOID>(address), size, old_protect, &discard);

        return write_ok && (bytes_written == size);
    }

    // =========================================================================
    // EnvironmentBridge Implementation
    // =========================================================================

    EnvironmentBridge::EnvironmentBridge(std::shared_ptr<ProcessAttachment> attachment)
        : attachment_(std::move(attachment))
    {
    }

    EnvironmentBridge::~EnvironmentBridge()
    {
        bridge_buffer_.release();
        script_payload_buffer_.release();
    }

    bool EnvironmentBridge::initialize()
    {
        if (!attachment_ || !attachment_->is_attached())
            return false;

        // Verify that target memory space and base pointer are accessible
        if (attachment_->target_base() == 0)
            return false;

        // Resolve essential RVA anchors
        context_descriptor_.task_scheduler_addr = attachment_->rebase_remote(rva::task_scheduler);
        context_descriptor_.script_context_addr = attachment_->rebase_remote(rva::script_context_resume);
        context_descriptor_.identity_struct_addr = attachment_->rebase_remote(rva::tls_identity_struct);
        context_descriptor_.capabilities = 0x3FFFFFF; // Maximum security context permissions
        context_descriptor_.execution_flags = 0x1;    // Active runtime execution flag
        context_descriptor_.status_code = 0x100;      // STATUS_LINK_SYNCHRONIZED

        if (!allocate_bridge_buffers())
            return false;

        initialized_ = true;
        return true;
    }

    bool EnvironmentBridge::allocate_bridge_buffers(std::size_t script_buffer_size)
    {
        HANDLE handle = attachment_->process_handle();

        // 1. Allocate control descriptor frame (Read-Write)
        bridge_buffer_ = VirtualMemoryRegion(handle, sizeof(BridgeContextDescriptor), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!bridge_buffer_.is_allocated())
            return false;

        // Commit initial descriptor into target process space
        if (!bridge_buffer_.write(0, &context_descriptor_, sizeof(BridgeContextDescriptor)))
            return false;

        // 2. Allocate runtime script staging area (Read-Write-Execute capable)
        script_payload_buffer_ = VirtualMemoryRegion(handle, script_buffer_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!script_payload_buffer_.is_allocated())
            return false;

        return true;
    }

    bool EnvironmentBridge::verify_engine_anchors() const
    {
        if (!attachment_ || !attachment_->is_attached())
            return false;

        // Verify task scheduler pointer resolution
        const std::uintptr_t task_scheduler_ptr_addr = attachment_->rebase_remote(rva::task_scheduler);
        if (task_scheduler_ptr_addr == 0)
            return false;

        std::uintptr_t task_scheduler_instance = 0;
        if (!attachment_->read(task_scheduler_ptr_addr, task_scheduler_instance))
            return false;

        // Verify Luau VM dispatcher entry
        const std::uintptr_t luau_exec = attachment_->rebase_remote(rva_vm::luau_execute);
        if (luau_exec == 0)
            return false;

        std::uint8_t op_byte = 0;
        if (!attachment_->read(luau_exec, op_byte))
            return false;

        return true;
    }

    bool EnvironmentBridge::stage_script_payload(std::string_view bytecode_or_source)
    {
        if (!script_payload_buffer_.is_allocated())
            return false;

        const std::uint32_t payload_length = static_cast<std::uint32_t>(bytecode_or_source.size());
        if (payload_length + sizeof(std::uint32_t) > script_payload_buffer_.size())
            return false;

        // Write length header
        if (!script_payload_buffer_.write(0, &payload_length, sizeof(std::uint32_t)))
            return false;

        // Write payload content
        if (!script_payload_buffer_.write(sizeof(std::uint32_t), bytecode_or_source.data(), payload_length))
            return false;

        return true;
    }
}
