#pragma once

#include <insti/actions/action.h>
#include <string>
#include <pnq/pnq.h>

namespace insti
{

    /// Manages a single entry in a REG_MULTI_SZ registry value.
    ///
    /// REG_MULTI_SZ is a Windows registry type that stores multiple strings
    /// as a null-separated list. This action adds or removes a specific entry
    /// without affecting other entries in the list.
    ///
    /// On backup, records whether the entry was present ("present" or "absent").
    /// On restore, adds or removes the entry accordingly.
    /// On clean, removes the entry from the list.
    class MultiStringEntryAction : public IAction
    {
        PNQ_DECLARE_NON_COPYABLE(MultiStringEntryAction)

    public:
        static constexpr std::string_view TYPE_NAME = "multistring";

        /// @param key Registry key path
        /// @param value_name Registry value name within the key
        /// @param entry The entry to add/remove from the list
        /// @param archive_path Path within the snapshot (stores "present" or "absent")
        /// @param description Optional user-facing description
        MultiStringEntryAction(std::string key, std::string value_name, std::string entry,
                               std::string archive_path, std::string description = {});

        /// @name Accessors for inspection and testing
        /// @{
        const std::string &key() const { return m_key; }
        const std::string &value_name() const { return m_value_name; }
        const std::string &entry() const { return m_entry; }
        const std::string &archive_path() const { return m_archive_path; }
        /// @}

    private:
        std::vector<std::pair<std::string, std::string>> to_params() const override;
        bool backup(ActionContext *ctx) const override;
        bool restore(ActionContext *ctx) const override;
        bool do_clean(ActionContext *ctx) const override;
        VerifyResult verify(ActionContext *ctx) const override;

        std::vector<std::string> read_multi_string() const;
        bool write_multi_string(const std::vector<std::string> &entries) const;
        bool is_in_list() const;
        bool add_to_list() const;
        bool remove_from_list() const;

        const std::string m_key;
        const std::string m_value_name;
        const std::string m_entry;
        const std::string m_archive_path;
    };

} // namespace insti
