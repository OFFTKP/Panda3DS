#pragma once

#include <cstdint>
#include "helpers.hpp"

// A packet is essentially just a bunch of bytes. The first byte is an identifier telling us what kind of packet it is.
// There's packets with a fixed size and packets with a variable size.

// We split those into a few types:
// STNS = Server (as in, the actual game room server) to Netplay Middleman Server
// NSTS = Netplay Middleman Server to Server
// CTNS = Client (as in, someone joining a game room) to Netplay Middleman Server
// NTCS = Netplay Middleman Server to Client
// PTP = Peer to Peer

enum Packet : u8 {
    CTS_CREATE_SERVER = 0x10,
    CTS_JOIN_SERVER = 0x11,

    STC_SERVER_CREATED = 0x20,
};

constexpr bool isPacketSizeFixed(Packet packet) {
    switch (packet) {
        default: return false;
    }
}