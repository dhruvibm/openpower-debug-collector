#pragma once

#include <errl_handle.H>

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

namespace openpower::dump::phal::error
{
/** Retain service callouts without automatic target actions for observations. */
inline nlohmann::json reportOnlyCallouts(nlohmann::json callouts)
{
    for (auto& callout : callouts)
    {
        callout["Deconfigured"] = false;
        callout["Guarded"] = false;
        callout.erase("GuardType");
    }
    return callouts;
}

/**
 * @brief Publish every HostFW entry without changing the collection outcome.
 *
 * The publisher receives the entry's terminal classification independently of
 * its native severity. A failed publication is reported and later entries are
 * still attempted. The last successful PEL ID is retained for dump tracking.
 */
template <typename Publisher, typename FailureReporter>
uint32_t publishHostfwEntries(const errl::ErrlHandle& errors,
                              Publisher&& publish, FailureReporter&& failed)
{
    uint32_t lastPelId = 0;
    const auto terminalCount = errors.getTerminalEntries().size();
    size_t index = 0;
    for (const auto& entry : errors.getEntries())
    {
        const bool terminal = index++ < terminalCount;
        try
        {
            const auto id = publish(*entry, terminal);
            if (id == 0)
            {
                throw std::runtime_error("PEL service returned a zero PEL ID");
            }
            lastPelId = id;
        }
        catch (const std::exception& exception)
        {
            failed(*entry, exception.what());
        }
        catch (...)
        {
            failed(*entry, "Unknown PEL publication failure");
        }
    }
    return lastPelId;
}
} // namespace openpower::dump::phal::error
