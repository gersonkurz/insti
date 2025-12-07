#include "pch.h"
#include <insti/actions/service.h>
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>
#include <pnq/win32/service.h>
#include <toml++/toml.hpp>

namespace insti
{

    ServiceAction::ServiceAction(std::string name, std::string archive_path, std::string description)
        : IAction{std::string{TYPE_NAME}, description.empty() ? "Service: " + name : std::move(description)}
        , m_name{std::move(name)}, m_archive_path{std::move(archive_path)}
    {
    }

    // ============================================================================
    // ServiceConfig TOML serialization
    // ============================================================================

    std::string ServiceConfig::to_toml() const
    {
        toml::table tbl;
        tbl.insert("name", name);
        tbl.insert("display_name", display_name);
        tbl.insert("description", description);
        tbl.insert("binary_path", binary_path);
        tbl.insert("start_type", static_cast<int64_t>(start_type));
        tbl.insert("service_type", static_cast<int64_t>(service_type));
        tbl.insert("account", account);
        tbl.insert("was_running", was_running);

        toml::array deps;
        for (const auto &dep : dependencies)
            deps.push_back(dep);
        tbl.insert("dependencies", deps);

        std::ostringstream oss;
        oss << tbl;
        return oss.str();
    }

    std::optional<ServiceConfig> ServiceConfig::from_toml(std::string_view toml_str)
    {
        try
        {
            auto tbl = toml::parse(toml_str);

            ServiceConfig config;
            config.name = tbl["name"].value_or(std::string{});
            if (config.name.empty())
                return std::nullopt;

            config.display_name = tbl["display_name"].value_or(std::string{});
            config.description = tbl["description"].value_or(std::string{});
            config.binary_path = tbl["binary_path"].value_or(std::string{});
            config.start_type = static_cast<uint32_t>(tbl["start_type"].value_or(int64_t{0}));
            config.service_type = static_cast<uint32_t>(tbl["service_type"].value_or(int64_t{0}));
            config.account = tbl["account"].value_or(std::string{});
            config.was_running = tbl["was_running"].value_or(false);

            if (auto *arr = tbl["dependencies"].as_array())
            {
                for (const auto &elem : *arr)
                {
                    if (auto *str = elem.as_string())
                        config.dependencies.push_back(str->get());
                }
            }

            return config;
        }
        catch (const toml::parse_error &e)
        {
            spdlog::error("Failed to parse service config TOML: {}", e.what());
            return std::nullopt;
        }
    }

    // ============================================================================
    // ServiceAction implementation
    // ============================================================================

    std::optional<ServiceConfig> ServiceAction::read_config() const
    {
        pnq::win32::SCM scm;
        if (!scm)
            return std::nullopt;

        auto svc = scm.open_service(m_name, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
        if (!svc)
            return std::nullopt;

        auto pnq_config = svc.query_config();
        if (!pnq_config)
            return std::nullopt;

        ServiceConfig result;
        result.name = pnq_config->name;
        result.display_name = pnq_config->display_name;
        result.description = pnq_config->description;
        result.binary_path = pnq_config->binary_path;
        result.account = pnq_config->account;
        result.dependencies = pnq_config->dependencies;
        result.start_type = pnq_config->start_type;
        result.service_type = pnq_config->service_type;
        result.was_running = svc.is_running();

        return result;
    }

    bool ServiceAction::write_config(const ServiceConfig &config) const
    {
        pnq::win32::SCM scm{SC_MANAGER_CREATE_SERVICE};
        if (!scm)
            return false;

        // Convert to pnq config
        pnq::win32::ServiceConfig pnq_config;
        pnq_config.name = config.name;
        pnq_config.display_name = config.display_name;
        pnq_config.description = config.description;
        pnq_config.binary_path = config.binary_path;
        pnq_config.account = config.account;
        pnq_config.dependencies = config.dependencies;
        pnq_config.start_type = config.start_type;
        pnq_config.service_type = config.service_type;

        // Try to open existing service first
        auto svc = scm.open_service(config.name, SERVICE_CHANGE_CONFIG);
        if (svc)
        {
            // Service exists, update it
            // Build dependencies as double-null-terminated string for change_config
            std::string deps;
            for (const auto &dep : config.dependencies)
            {
                deps += dep;
                deps += '\0';
            }

            if (!svc.change_config(
                    config.service_type,
                    config.start_type,
                    SERVICE_NO_CHANGE,
                    config.binary_path,
                    {},  // load order group
                    deps,
                    config.account,
                    {},  // password
                    config.display_name))
                return false;

            if (!config.description.empty())
                svc.set_description(config.description);

            return true;
        }

        // Create new service
        svc = scm.create_service(pnq_config);
        return static_cast<bool>(svc);
    }

    bool ServiceAction::stop_service() const
    {
        pnq::win32::SCM scm;
        if (!scm)
            return false;

        auto svc = scm.open_service(m_name, SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (!svc)
            return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;

        svc.stop();
        svc.wait_until_stopped();
        return true;
    }

    bool ServiceAction::start_service() const
    {
        pnq::win32::SCM scm;
        if (!scm)
            return false;

        auto svc = scm.open_service(m_name, SERVICE_START);
        if (!svc)
            return false;

        return svc.start();
    }

    bool ServiceAction::delete_service() const
    {
        // First stop the service
        stop_service();

        pnq::win32::SCM scm;
        if (!scm)
            return false;

        auto svc = scm.open_service(m_name, DELETE);
        if (!svc)
            return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;

        return svc.remove();
    }

    bool ServiceAction::backup(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Backup", description().c_str(), -1);

        // Read current service config
        auto config = read_config();
        if (!config)
        {
            if (cb)
            {
                auto decision = cb->on_error("Service does not exist", m_name.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::warn("Service does not exist: {}", m_name);
            return true;
        }

        // Write to snapshot as TOML
        if (!ctx->writer()->write_text(m_archive_path, config->to_toml()))
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to write service config to snapshot", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write service config to snapshot: {}", m_archive_path);
            return false;
        }

        return true;
    }

    bool ServiceAction::restore(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        if (cb)
            cb->on_progress("Restore", description().c_str(), -1);

        if (!check_archive_exists(m_archive_path, ctx))
            return true;

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would restore service: {}", m_name);
            if (cb)
                cb->on_progress("Simulate", std::string("Would restore service: ") + m_name, -1);
            return true;
        }

        // Read TOML from snapshot
        std::string toml_content = ctx->reader()->read_text(m_archive_path);
        if (toml_content.empty())
        {
            if (cb)
                cb->on_warning("Empty service config in snapshot");
            return true;
        }

        // Parse config
        auto config = ServiceConfig::from_toml(toml_content);
        if (!config)
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to parse service config from snapshot", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to parse service config from snapshot: {}", m_archive_path);
            return false;
        }

        // Write config (creates or updates service)
        if (!write_config(*config))
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to restore service", m_name.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to restore service: {}", m_name);
            return false;
        }

        // Start service if it was running at backup time
        if (config->was_running)
        {
            if (!start_service())
            {
                if (cb)
                    cb->on_warning(("Failed to start service: " + m_name).c_str());
                // Not a fatal error - service is restored, just not started
            }
        }

        return true;
    }

    bool ServiceAction::do_clean(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would delete service: {}", m_name);
            if (cb)
                cb->on_progress("Simulate", std::string("Would delete service: ") + m_name, -1);
            return true;
        }

        return delete_service();
    }

    VerifyResult ServiceAction::verify(ActionContext *ctx) const
    {
        // Check if service exists on system
        auto system_config = read_config();
        bool exists_on_system = system_config.has_value();

        // Check if exists in snapshot (if reader available)
        bool exists_in_snapshot = false;
        if (ctx->reader())
            exists_in_snapshot = ctx->reader()->exists(m_archive_path);

        if (!exists_on_system && !exists_in_snapshot)
        {
            return {VerifyResult::Status::Missing, "Service not found on system or in snapshot"};
        }

        if (exists_on_system && !exists_in_snapshot && ctx->reader())
        {
            return {VerifyResult::Status::Extra, "Service exists on system but not in snapshot"};
        }

        if (!exists_on_system && exists_in_snapshot)
        {
            return {VerifyResult::Status::Missing, "Service exists in snapshot but not on system"};
        }

        // Both exist - for now just report match
        // TODO: Could compare config values
        return {VerifyResult::Status::Match, "Service exists"};
    }

    std::vector<std::pair<std::string, std::string>> ServiceAction::to_params() const
    {
        return {
            {"name", m_name},
            {"archive", m_archive_path}};
    }

} // namespace insti
