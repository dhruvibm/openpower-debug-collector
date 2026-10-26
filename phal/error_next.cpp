#include "../dump/dump_utils.hpp"
#include "error_iface.hpp"
#include "hostfw_error_publish.hpp"

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
static std::string getSeverityLevel(int severity)
{
    using Level =
        sdbusplus::xyz::openbmc_project::Logging::server::Entry::Level;

    // Map severity to Level enum
    // Severity levels from errl: 0=Emergency, 1=Alert, 2=Critical, 3=Error,
    // etc. Map to Logging.Entry.Level
    Level level = Level::Error; // Default
    if (severity <= 2)
    {
        level = Level::Critical;
    }
    else if (severity == 3)
    {
        level = Level::Error;
    }
    else if (severity == 4)
    {
        level = Level::Warning;
    }
    else
    {
        level = Level::Informational;
    }

    return sdbusplus::xyz::openbmc_project::Logging::server::convertForMessage(
        level);
}

namespace
{
struct OpenedFfdcFiles
{
    std::vector<int> descriptors;
    ~OpenedFfdcFiles()
    {
        for (const int fd : descriptors)
        {
            ::close(fd);
        }
    }
};

using FfdcFiles = std::vector<std::tuple<
    DBusFFDCFormat, uint8_t, uint8_t, sdbusplus::message::unix_fd>>;

void appendFfdcFiles(const errl::ErrlEntry& entry, FfdcFiles& files,
                     OpenedFfdcFiles& opened)
{
    if (const auto& attachments = entry.getFfdcFiles(); attachments)
    {
        for (const auto& [attachment, path] : *attachments)
        {
            int fd = attachment.fd;
            if (fd < 0)
            {
                fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
                if (fd < 0)
                {
                    throw std::runtime_error("Cannot open HostFW FFDC: " + path.string());
                }
                opened.descriptors.push_back(fd);
            }
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
            files.emplace_back(format, attachment.subType, attachment.version,
                               sdbusplus::message::unix_fd{fd});
        }
    }
}
} // namespace

uint32_t commitHostfwError(errl::ErrlHandleOpt&& err)
{
    if (!err || !err->hasEntries())
    {
        return 0;
    }
    return publishHostfwEntries(
        *err,
        [](const errl::ErrlEntry& entry) {
            auto bus = sdbusplus::bus::new_default();
            constexpr auto path = "/xyz/openbmc_project/logging";
            constexpr auto interface = "org.open_power.Logging.PEL";
            std::map<std::string, std::string> data(
                entry.getAdditionalData().begin(), entry.getAdditionalData().end());
            data["_PID"] = std::to_string(getpid());

            // Observation classification changes the operation outcome only;
            // retain the severity and requested callout actions for publication.
            OpenedFfdcFiles opened;
            FfdcFiles files;
            appendFfdcFiles(entry, files, opened);
            const auto service = openpower::dump::util::getService(bus, interface, path);
            auto method = bus.new_method_call(service.c_str(), path, interface,
                                              "CreatePELWithFFDCFiles");
            method.append(entry.getMessage(),
                          getSeverityLevel(static_cast<int>(entry.getSeverity())), data, files);
            auto reply = bus.call(method);
            std::tuple<uint32_t, uint32_t> ids{};
            reply.read(ids);
            return std::get<1>(ids);
        },
        [](const errl::ErrlEntry& entry, const char* detail) {
            lg2::error("Failed to publish HostFW entry {ENTRY}: {ERROR}",
                       "ENTRY", entry.getMessage(), "ERROR", detail);
        });
}
#endif // NEXT_PHAL

} // namespace openpower::dump::phal::error
