#pragma once

#include "PacketDispatcher.h"
#include "PacketReceiveStats.h"
#include "PacketStreamParser.h"
#include "myiperf/NetworkInterface.h"
#include "myiperf/CoroutineSupport.h"

#include <atomic>
#include <memory>

class ControlMessageBus;

/**
 * @class PacketReceiver
 * @brief Owns the receive coroutine lifecycle.
 *
 * PacketReceiver keeps the async receive loop readable. PacketStreamParser
 * handles framing and validation, PacketReceiveStats owns counters, and
 * PacketDispatcher routes parsed packets to stats or ControlMessageBus.
 */
class PacketReceiver {
public:
    /**
     * @brief Constructs a PacketReceiver.
     * @param netInterface A pointer to the NetworkInterface to use for receiving data.
     */
    PacketReceiver(NetworkInterface* netInterface);

    /**
     * @brief Destroys the PacketReceiver object.
     */
    ~PacketReceiver();

    void start(ControlMessageBus& messages);

    /**
     * @brief Stops the packet receiving process.
     */
    void stop();

    /**
     * @brief Retrieves the current receiver statistics.
     * @return A TestStats struct containing the latest statistics. This method is thread-safe.
     */
    TestStats getStats() const;

    /**
     * @brief Resets all statistical counters to zero.
     * This is useful for clearing stats from a previous run without re-creating the object.
     */
    void resetStats();

private:
    Task receiverLoop();

    NetworkInterface* networkInterface;
    std::atomic<bool> running;
    size_t packetBufferSize;
    PacketStreamParser parser;
    PacketReceiveStats stats;
    std::unique_ptr<PacketDispatcher> dispatcher;
    Task receiverTask{nullptr};
};
