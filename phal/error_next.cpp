#include "../dump/dump_utils.hpp"
#include "error_iface.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <xyz/openbmc_project/Logging/Create/server.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace openpower::dump::phal::error
{

using DBusFFDCFormat =
    sdbusplus::xyz::openbmc_project::Logging::server::Create::FFDCFormat;

bool logChipOpError([[maybe_unused]] const chipop::ChipOpError& err,
                    [[maybe_unused]] targeting::TargetHandle chip,
                    [[maybe_unused]] uint32_t cmdClass,
                    [[maybe_unused]] uint32_t cmdType,
                    [[maybe_unused]] const std::filesystem::path& dumpPath)
{
    // Stub: Not implemented for next backend
    return false;
}

uint32_t createChipOpErrorPEL(
    [[maybe_unused]] const chipop::ChipOpError& err,
    [[maybe_unused]] targeting::TargetHandle chip,
    [[maybe_unused]] const std::string& event,
    [[maybe_unused]] const std::filesystem::path& dumpPath)
{
    // Stub: Not implemented for next backend
    return 0;
}

std::tuple<uint32_t, std::string> getPelInfo([[maybe_unused]] uint32_t logId)
{
    // Stub: Not implemented for next backend
    return {0, ""};
}

#ifdef NEXT_PHAL
// Helper to convert errl severity to D-Bus level string
static std::string getSeverityLevel(errl::Level severity)
{
    using Level =
        sdbusplus::xyz::openbmc_project::Logging::server::Entry::Level;
    Level level = Level::Error;
    switch (severity)
    {
        case errl::Level::Emergency:
            level = Level::Emergency;
            break;
        case errl::Level::Alert:
            level = Level::Alert;
            break;
        case errl::Level::Critical:
            level = Level::Critical;
            break;
        case errl::Level::Error:
            level = Level::Error;
            break;
        case errl::Level::Warning:
            level = Level::Warning;
            break;
        case errl::Level::Notice:
            level = Level::Notice;
            break;
        case errl::Level::Informational:
            level = Level::Informational;
            break;
        case errl::Level::Debug:
            level = Level::Debug;
            break;
    }
    return sdbusplus::xyz::openbmc_project::Logging::server::convertForMessage(
        level);
}

uint32_t commitHostfwError(errl::ErrlHandleOpt&& err)
{
    if (!err || !err->hasEntries())
    {
        return 0;
    }

    uint32_t pelId = 0;
    for (const auto& entry : err->getEntries())
    {
        std::vector<int> openedFds;
        try
        {
            auto bus = sdbusplus::bus::new_default();
            constexpr auto path = "/xyz/openbmc_project/logging";
            constexpr auto interface = "org.open_power.Logging.PEL";
            std::map<std::string, std::string> data(
                entry->getAdditionalData().begin(),
                entry->getAdditionalData().end());
            data["_PID"] = std::to_string(getpid());
            std::vector<std::tuple<DBusFFDCFormat, uint8_t, uint8_t,
                                   sdbusplus::message::unix_fd>>
                files;
            if (const auto& attachments = entry->getFfdcFiles(); attachments)
            {
                for (const auto& [attachment, filePath] : *attachments)
                {
                    const int fd =
                        ::open(filePath.c_str(), O_RDONLY | O_CLOEXEC);
                    if (fd < 0)
                    {
                        throw std::runtime_error(
                            "Cannot open HostFW FFDC: " + filePath.string());
                    }
                    openedFds.push_back(fd);
                    DBusFFDCFormat format = DBusFFDCFormat::Custom;
                    switch (attachment.format)
                    {
                        case errl::FFDCFormat::JSON:
                            format = DBusFFDCFormat::JSON;
                            break;
                        case errl::FFDCFormat::CBOR:
                            format = DBusFFDCFormat::CBOR;
                            break;
                        case errl::FFDCFormat::Text:
                            format = DBusFFDCFormat::Text;
                            break;
                        case errl::FFDCFormat::Custom:
                            break;
                    }
                    files.emplace_back(format, attachment.subType,
                                       attachment.version,
                                       sdbusplus::message::unix_fd{fd});
                }
            }
            const auto service =
                openpower::dump::util::getService(bus, interface, path);
            auto method = bus.new_method_call(service.c_str(), path, interface,
                                              "CreatePELWithFFDCFiles");
            method.append(entry->getMessage(),
                          getSeverityLevel(entry->getSeverity()), data, files);
            auto reply = bus.call(method);
            std::tuple<uint32_t, uint32_t> ids{};
            reply.read(ids);
            const auto createdId = std::get<1>(ids);
            if (createdId == 0)
            {
                throw std::runtime_error("PEL service returned a zero PEL ID");
            }
            pelId = createdId;
        }
        catch (const std::exception& e)
        {
            lg2::error("Failed to publish HostFW entry {ENTRY}: {ERROR}",
                       "ENTRY", entry->getMessage(), "ERROR", e.what());
        }
        for (const int fd : openedFds)
        {
            ::close(fd);
        }
    }
    return pelId;
}
#endif // NEXT_PHAL

} // namespace openpower::dump::phal::error
