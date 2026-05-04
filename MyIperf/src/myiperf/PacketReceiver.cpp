#include "PacketReceiver.h"

#include "ControlMessageBus.h"
#include "myiperf/Logger.h"

namespace {

constexpr size_t RECEIVE_BUFFER_SIZE = 65536;

} // namespace

PacketReceiver::PacketReceiver(NetworkInterface* netInterface)
    : networkInterface(netInterface),
      running(false),
      packetBufferSize(RECEIVE_BUFFER_SIZE),
      parser(RECEIVE_BUFFER_SIZE * 2) {}

PacketReceiver::~PacketReceiver() {
    stop();
}

void PacketReceiver::start(ControlMessageBus& messages) {
    if (running) {
        Logger::log("Info: PacketReceiver is already running.");
        return;
    }

    parser.reset();
    stats.reset();
    dispatcher = std::make_unique<PacketDispatcher>(messages, stats);
    running = true;

    Logger::log("Info: PacketReceiver started.");

    receiverTask = receiverLoop();
    receiverTask.start();
}

void PacketReceiver::stop() {
    if (!running.exchange(false)) {
        return;
    }
    Logger::log("Info: PacketReceiver stopped.");
}

TestStats PacketReceiver::getStats() const {
    return stats.snapshot();
}

void PacketReceiver::resetStats() {
    stats.reset();
    Logger::log("Info: PacketReceiver statistics have been reset.");
}

Task PacketReceiver::receiverLoop() {
    while (running) {
        try {
            auto result = co_await networkInterface->receive(packetBufferSize);

            if (result.bytesReceived == 0) {
                Logger::log("Warning: 0 bytes received. The connection may have been closed.");
                auto parsed = parser.drainPackets();
                for (size_t i = 0; i < parsed.checksumFailures; ++i) {
                    stats.onChecksumFailure();
                }
                if (dispatcher) {
                    dispatcher->dispatch(parsed.packets);
                }
                stop();
                break;
            }

            parser.append(result.data, result.bytesReceived);
            auto parsed = parser.drainPackets();
            for (size_t i = 0; i < parsed.checksumFailures; ++i) {
                stats.onChecksumFailure();
            }
            if (dispatcher) {
                dispatcher->dispatch(parsed.packets);
            }
        } catch (const std::exception& e) {
            Logger::log("Error in receiver loop: " + std::string(e.what()));
            stop();
            break;
        }
    }
}
