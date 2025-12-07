#pragma once

#include <insti/actions/action.h>
#include <string>
#include <pnq/pnq.h>

namespace insti
{

    /// Manages a single entry in a delimited REG_SZ registry value.
    ///
    /// Use this for semicolon-delimited (or other delimiter) list values like
    /// PATH, INCLUDE, LIB, etc. Unlike capturing the entire value, this action
    /// adds or removes a specific entry without affecting others.
    ///
    /// On backup, records whether the entry was present ("present" or "absent").
    /// On restore, adds or removes the entry accordingly.
    /// On clean, removes the entry from the list.
    class DelimitedEntryAction : public IAction
    {
        PNQ_DECLARE_NON_COPYABLE(DelimitedEntryAction)

    public:
        static constexpr std::string_view TYPE_NAME = "delimited";

        /// Where to insert new entries in the list.
        enum class InsertPosition
        {
            Prepend, ///< Insert at beginning (higher priority for PATH-like vars)
            Append   ///< Insert at end (default, lower priority)
        };

        /// @param key Registry key path (e.g., "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment")
        /// @param value_name Registry value name within the key (e.g., "Path")
        /// @param entry The entry to add/remove from the list
        /// @param archive_path Path within the snapshot (stores "present" or "absent")
        /// @param delimiter Delimiter character(s) separating entries (default ";")
        /// @param insert_pos Where to insert new entries (default: Append)
        /// @param description Optional user-facing description
        DelimitedEntryAction(std::string key, std::string value_name, std::string entry,
                             std::string archive_path, std::string delimiter = ";",
                             InsertPosition insert_pos = InsertPosition::Append,
                             std::string description = {});

        /// @name Accessors for inspection and testing
        /// @{
        const std::string &key() const { return m_key; }
        const std::string &value_name() const { return m_value_name; }
        const std::string &entry() const { return m_entry; }
        const std::string &delimiter() const { return m_delimiter; }
        const std::string &archive_path() const { return m_archive_path; }
        InsertPosition insert_position() const { return m_insert_pos; }
        /// @}

    private:
        std::vector<std::pair<std::string, std::string>> to_params() const override;
        bool backup(ActionContext *ctx) const override;
        bool restore(ActionContext *ctx) const override;
        bool do_clean(ActionContext *ctx) const override;
        VerifyResult verify(ActionContext *ctx) const override;

        std::string read_value() const;
        bool write_value(const std::string &value) const;
        bool is_in_list() const;
        bool add_to_list() const;
        bool remove_from_list() const;

        const std::string m_key;
        const std::string m_value_name;
        const std::string m_entry;
        const std::string m_archive_path;
        const std::string m_delimiter;
        const InsertPosition m_insert_pos;
    };

} // namespace insti
