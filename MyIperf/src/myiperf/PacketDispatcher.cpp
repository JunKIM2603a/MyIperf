#include "PacketDispatcher.h"

#include "ControlMessageBus.h"
#include "ControlProtocol.h"
#include "myiperf/Logger.h"

PacketDispatcher::PacketDispatcher(ControlMessageBus& messages,
                                   PacketReceiveStats& stats)
    : messages(messages), stats(stats) {}

void PacketDispatcher::dispatch(const std::vector<ParsedPacket>& packets) {
    for (const auto& packet : packets) {
#ifdef DEBUG_LOG
        Logger::log("Debug: PacketDispatcher dispatching packet. Message Type: "
                    + std::to_string(static_cast<int>(packet.header.messageType))
                    + ", Packet Counter: "
                    + std::to_string(packet.header.packetCounter)
                    + ", payloadSize: "
                    + std::to_string(packet.header.payloadSize));
#endif

        if (packet.header.messageType == MessageType::DATA_PACKET) {
            stats.onDataPacket(packet);
            Logger::log("Info: PacketReceiver received DATA_PACKET "
                        + std::to_string(packet.header.packetCounter)
                        + " (size: " + std::to_string(packet.totalPacketSize)
                        + " bytes)");
            continue;
        }

        Logger::log("CONTROL: PacketReceiver forwarding control message "
                    + std::string(ControlProtocol::messageTypeToString(
                          packet.header.messageType))
                    + " (messageType="
                    + std::to_string(static_cast<int>(packet.header.messageType))
                    + ", packetCounter="
                    + std::to_string(packet.header.packetCounter)
                    + ", totalPacketSize="
                    + std::to_string(packet.totalPacketSize)
                    + " bytes, payloadSize="
                    + std::to_string(packet.header.payloadSize)
                    + " bytes)");
        messages.deliver(packet.header, packet.payload);
    }
}
