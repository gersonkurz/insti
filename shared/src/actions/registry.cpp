#include "pch.h"
#include <insti/actions/registry.h>
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/core/blueprint.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>
#include <pnq/regis3.h>
#include <sddl.h>
#include <aclapi.h>

namespace insti
{

namespace
{

/// Set permissive SDDL on a registry key (Everyone: Full Control)
/// Applies recursively to the key and all subkeys
void set_permissive_registry_sddl(const std::string& key_path)
{
    // SDDL: D:(A;OICI;GA;;;WD) = Allow Everyone Generic All, Object Inherit + Container Inherit
    PSECURITY_DESCRIPTOR pSD = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;OICI;GA;;;WD)", SDDL_REVISION_1, &pSD, nullptr))
    {
        spdlog::warn("Failed to create security descriptor for registry");
        return;
    }

    BOOL daclPresent = FALSE, daclDefaulted = FALSE;
    PACL pDacl = nullptr;
    if (!GetSecurityDescriptorDacl(pSD, &daclPresent, &pDacl, &daclDefaulted))
    {
        LocalFree(pSD);
        return;
    }

    // Open the key with WRITE_DAC permission
    pnq::regis3::key key{key_path};
    if (!key.open_for_writing())
    {
        LocalFree(pSD);
        return;
    }

    DWORD result = SetSecurityInfo(
        key.handle(),
        SE_REGISTRY_KEY,
        DACL_SECURITY_INFORMATION,
        nullptr, nullptr, pDacl, nullptr);

    if (result != ERROR_SUCCESS)
        spdlog::debug("SetSecurityInfo for registry key failed: {}", result);

    LocalFree(pSD);
}

} // anonymous namespace

    bool RegistryAction::backup(ActionContext *ctx) const
    {
        std::string resolved_key = ctx->blueprint()->resolve(m_key);
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Backup", description().c_str(), -1);

        // Import from live registry
        pnq::regis3::registry_importer importer{resolved_key};
        pnq::regis3::key_entry *root = importer.import();
        if (!root)
        {
            if (cb)
            {
                auto decision = cb->on_error("Registry key does not exist", resolved_key.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::warn("Registry key does not exist: {}", resolved_key);
            return true;
        }

        // Export to .REG format string (UTF-8)
        pnq::regis3::regfile_format5_exporter exporter;
        if (!exporter.perform_export(root))
        {
            PNQ_RELEASE(root);
            if (cb)
            {
                auto decision = cb->on_error("Failed to export registry key", resolved_key.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to export registry key: {}", resolved_key);
            return false;
        }

        PNQ_RELEASE(root);

        // Reverse variable substitution (make portable)
        std::string reg_content = ctx->blueprint()->unresolve(exporter.result());

        // Write to snapshot as UTF-16LE (proper .reg format)
        if (!ctx->writer()->write_utf16(m_archive_path, reg_content))
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to write registry to snapshot", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write registry to snapshot: {}", m_archive_path);
            return false;
        }

        return true;
    }

    bool RegistryAction::restore(ActionContext *ctx) const
    {
        std::string resolved_key = ctx->blueprint()->resolve(m_key);
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        if (cb)
            cb->on_progress("Restore", description().c_str(), -1);

        if (!check_archive_exists(m_archive_path, ctx))
            return true;

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would restore registry: {} -> {}", m_archive_path, resolved_key);
            if (cb)
                cb->on_progress("Simulate", std::string("Would restore registry: ") + resolved_key, -1);
            return true;
        }

        // Read .REG content from snapshot (auto-detects UTF-16LE, returns UTF-8)
        std::string reg_content = ctx->reader()->read_text(m_archive_path);
        if (reg_content.empty())
        {
            if (cb)
                cb->on_warning("Empty registry file in snapshot");
            return true;
        }

        // Resolve variables (e.g., ${COMPUTERNAME} -> actual value)
        reg_content = ctx->blueprint()->resolve(reg_content);

        // Parse .REG file
        auto importer = pnq::regis3::create_importer_from_string(reg_content);
        if (!importer)
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to parse registry file", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to parse registry file: {}", m_archive_path);
            return false;
        }

        pnq::regis3::key_entry *root = importer->import();
        if (!root)
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to import registry file", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to import registry file: {}", m_archive_path);
            return false;
        }

        // Export to live registry
        pnq::regis3::registry_exporter exporter;
        bool success = exporter.perform_export(root);

        PNQ_RELEASE(root);

        if (success)
        {
            // Set permissive SDDL so non-admin users can access the registry keys
            set_permissive_registry_sddl(resolved_key);
        }

        if (!success)
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to write registry key", resolved_key.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write registry key: {}", resolved_key);
            return false;
        }

        return true;
    }

    bool RegistryAction::do_clean(ActionContext *ctx) const
    {
        std::string resolved_key = ctx->blueprint()->resolve(m_key);
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        // Check if key exists first
        pnq::regis3::key test_key{resolved_key};
        if (!test_key.open_for_reading())
        {
            // Key doesn't exist - nothing to clean
            return true;
        }
        test_key.close();

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would delete registry key: {}", resolved_key);
            if (cb)
                cb->on_progress("Simulate", std::string("Would delete registry: ") + resolved_key, -1);
            return true;
        }

        // Take ownership recursively before deletion
        spdlog::debug("Taking ownership of registry key: {}", resolved_key);
        if (!pnq::regis3::key::take_ownership_recursive(resolved_key))
        {
            spdlog::warn("Failed to take ownership of registry key: {}", resolved_key);
            // Continue anyway - delete might still work
        }

        // Delete the key recursively
        spdlog::debug("Deleting registry key: {}", resolved_key);
        if (!pnq::regis3::key::delete_recursive(resolved_key))
        {
            // Check if key still exists
            pnq::regis3::key check{resolved_key};
            if (check.open_for_reading())
            {
                if (cb && !ctx->skip_all_errors())
                {
                    auto decision = cb->on_error("Failed to delete registry key", resolved_key.c_str());
                    if (decision == IActionCallback::Decision::Abort)
                        return false;
                    if (decision == IActionCallback::Decision::SkipAll)
                        ctx->set_skip_all_errors(true);
                }
                return false;
            }
        }

        return true;
    }

    VerifyResult RegistryAction::verify(ActionContext *ctx) const
    {
        // Resolve key path variables
        std::string resolved_key = ctx->blueprint()->resolve(m_key);

        // Check if key exists on system
        pnq::regis3::key test_key{resolved_key};
        bool exists_on_system = test_key.open_for_reading();

        // Check if exists in snapshot (if reader available)
        bool exists_in_snapshot = false;
        if (ctx->reader())
            exists_in_snapshot = ctx->reader()->exists(m_archive_path);

        if (!exists_on_system && !exists_in_snapshot)
        {
            return {VerifyResult::Status::Missing, "Registry key not found on system or in snapshot"};
        }

        if (exists_on_system && !exists_in_snapshot && ctx->reader())
        {
            return {VerifyResult::Status::Extra, "Registry key exists on system but not in snapshot"};
        }

        if (!exists_on_system && exists_in_snapshot)
        {
            return {VerifyResult::Status::Missing, "Registry key exists in snapshot but not on system"};
        }

        // Both exist - for now just report match
        // TODO: Could compare values
        return {VerifyResult::Status::Match, "Registry key exists"};
    }

    std::vector<std::pair<std::string, std::string>> RegistryAction::to_params() const
    {
        return {
            {"key", m_key},
            {"archive", m_archive_path}};
    }

} // namespace insti
