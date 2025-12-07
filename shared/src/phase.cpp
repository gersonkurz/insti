#include "pch.h"
#include <insti/core/phase.h>

namespace insti
{

bool parse_phase(std::string_view str, Phase& out_phase)
{
    // Accept both "PreBackup" and "pre-backup" formats (case-insensitive)
    if (pnq::string::equals_nocase(str, "PreBackup") || pnq::string::equals_nocase(str, "pre-backup"))
    {
        out_phase = Phase::PreBackup;
        return true;
    }
    if (pnq::string::equals_nocase(str, "PostBackup") || pnq::string::equals_nocase(str, "post-backup"))
    {
        out_phase = Phase::PostBackup;
        return true;
    }
    if (pnq::string::equals_nocase(str, "PreRestore") || pnq::string::equals_nocase(str, "pre-restore"))
    {
        out_phase = Phase::PreRestore;
        return true;
    }
    if (pnq::string::equals_nocase(str, "PostRestore") || pnq::string::equals_nocase(str, "post-restore"))
    {
        out_phase = Phase::PostRestore;
        return true;
    }
    if (pnq::string::equals_nocase(str, "PreClean") || pnq::string::equals_nocase(str, "pre-clean"))
    {
        out_phase = Phase::PreClean;
        return true;
    }
    if (pnq::string::equals_nocase(str, "PostClean") || pnq::string::equals_nocase(str, "post-clean"))
    {
        out_phase = Phase::PostClean;
        return true;
    }
    return false;
}

} // namespace insti
