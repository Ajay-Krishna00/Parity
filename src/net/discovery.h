#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace parity {

// One discovered host on the LAN.
struct HostEntry {
    std::string name;
    std::string ip;
    uint16_t    port = 0;        // the host's game port
    uint32_t    lastSeenMs = 0;
};

// Server-free LAN matchmaking via UDP broadcast (the "Mini Militia" lobby).
//
//   Host:    startHost(name, gamePort) then tick() every frame -> broadcasts a
//            beacon to the whole subnet once a second.
//   Browser: startBrowse() then tick() every frame -> listens for beacons and
//            keeps a live list of hosts (entries expire a few seconds after the
//            last beacon). The host's IP comes from the packet source, so the
//            player never types or even sees an address.
//
// Windows-only (Winsock); lives in the net layer, never the deterministic sim.
class LanDiscovery {
public:
    static const uint16_t DISCOVERY_PORT = 50505;
    static const uint32_t BEACON_INTERVAL_MS = 1000;
    static const uint32_t EXPIRY_MS          = 4000;

    bool startHost(const std::string& name, uint16_t gamePort);
    bool startBrowse();
    void tick(uint32_t nowMs);   // host: broadcast; browser: receive + expire
    void stop();

    const std::vector<HostEntry>& hosts() const { return hostList; }

    // Best-effort local IPv4 (for the host screen's manual-connect fallback).
    static std::string localIpv4();

    ~LanDiscovery();

private:
    uintptr_t   sock = ~(uintptr_t)0;
    bool        isHost = false;
    std::string myName;
    uint16_t    myGamePort = 0;
    uint32_t    lastBeaconMs = 0;
    std::vector<HostEntry> hostList;

    void broadcast();
    void receive(uint32_t nowMs);
    void expire(uint32_t nowMs);
};

} // namespace parity
