#include "hostfw_error_publish.hpp"

#include <unistd.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace error = openpower::dump::phal::error;

TEST(HostfwErrorPublishTest, AttemptsEveryEntryAfterPublicationFailure)
{
    errl::ErrlHandle handle;
    handle.addEntry(std::make_unique<errl::ErrlEntry>(
        "terminal", errl::Level::Informational,
        std::unordered_map<std::string, std::string>{}));
    handle.addEntry(std::make_unique<errl::ErrlEntry>(
                        "observation", errl::Level::Error,
                        std::unordered_map<std::string, std::string>{}),
                    errl::ErrlHandle::EntryType::Observation);
    std::vector<std::string> attempts;
    int failures = 0;
    const auto pel = error::publishHostfwEntries(
        handle,
        [&](const errl::ErrlEntry& entry, bool terminal) -> uint32_t {
            attempts.push_back(entry.getMessage());
            if (terminal)
            {
                EXPECT_EQ(entry.getSeverity(), errl::Level::Informational);
                throw std::runtime_error("PEL unavailable");
            }
            EXPECT_EQ(entry.getSeverity(), errl::Level::Error);
            return 17;
        },
        [&](const errl::ErrlEntry&, const char*) { ++failures; });
    EXPECT_EQ(attempts, (std::vector<std::string>{"terminal", "observation"}));
    EXPECT_EQ(failures, 1);
    EXPECT_EQ(pel, 17U);
    EXPECT_TRUE(handle.hasTerminalErrors());
}

TEST(HostfwErrorPublishTest, PreservesWireAttachmentBytesSubtypeAndVersion)
{
    const std::vector<uint8_t> wire{0xEB, 0xAD, 0, 4, 1, 2, 3, 4};
    errl::ErrlHandle handle;
    handle.addEntry(std::make_unique<errl::ErrlEntry>(
                        "SBE FFDC", errl::Level::Error,
                        std::unordered_map<std::string, std::string>{},
                        std::nullopt, std::nullopt,
                        std::vector<errl::FfdcAttachment>{
                            {.subType = 0xCC, .version = 1, .data = wire}}),
                    errl::ErrlHandle::EntryType::Observation);
    int publications = 0;
    const auto pel = error::publishHostfwEntries(
        handle,
        [&](const errl::ErrlEntry& entry, bool terminal) -> uint32_t {
            ++publications;
            EXPECT_FALSE(terminal);
            EXPECT_EQ(entry.getSeverity(), errl::Level::Error);
            EXPECT_TRUE(entry.getFfdcFiles());
            const auto& attachment = entry.getFfdcFiles()->front().first;
            EXPECT_EQ(attachment.subType, 0xCC);
            EXPECT_EQ(attachment.version, 1);
            std::vector<uint8_t> actual(wire.size());
            EXPECT_EQ(pread(attachment.fd, actual.data(), actual.size(), 0),
                      static_cast<ssize_t>(wire.size()));
            EXPECT_EQ(actual, wire);
            return 18;
        },
        [](const errl::ErrlEntry&, const char* message) { FAIL() << message; });
    EXPECT_EQ(pel, 18U);
    EXPECT_EQ(publications, 1);
    EXPECT_FALSE(handle.hasTerminalErrors());
}

TEST(HostfwErrorPublishTest, ObservationCalloutsRetainIdentityWithoutActions)
{
    const nlohmann::json original = nlohmann::json::array(
        {{{"EntityPath", {1, 2, 3}}, {"Priority", "H"},
          {"Deconfigured", true}, {"Guarded", true}, {"GuardType", "FATAL"}}});
    const auto report = error::reportOnlyCallouts(original);
    EXPECT_EQ(report[0]["EntityPath"], original[0]["EntityPath"]);
    EXPECT_EQ(report[0]["Priority"], "H");
    EXPECT_EQ(report[0]["Deconfigured"], false);
    EXPECT_EQ(report[0]["Guarded"], false);
    EXPECT_FALSE(report[0].contains("GuardType"));
    EXPECT_EQ(original[0]["Deconfigured"], true);
    EXPECT_EQ(original[0]["Guarded"], true);
}
