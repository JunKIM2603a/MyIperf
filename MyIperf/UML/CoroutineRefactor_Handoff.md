# Coroutine Refactor Handoff

Date: 2026-04-30

This file records the interrupted refactor state. Read this before continuing the
coroutine/readability refactor. The code is intentionally mid-change and may not
compile until the remaining steps below are completed.

## Goal

Keep high-level test flow readable as sequential `co_await` code, while hiding
callbacks inside platform I/O implementation details. Split `PacketReceiver`
responsibilities into smaller classes:

- `PacketReceiver`: receive coroutine lifecycle only.
- `PacketStreamParser`: byte stream buffering, packet framing, start-code check,
  payload-size check, checksum validation.
- `PacketReceiveStats`: thread-safe receive statistics.
- `PacketDispatcher`: route `DATA_PACKET` to stats and control messages to
  `ControlMessageBus`.

Do not introduce `Task<T>` in this pass. Keep the current awaiter pattern.

## Current Partial State

Some changes have already been applied.

### NetworkInterface

`include/myiperf/NetworkInterface.h` has been changed so awaiters call protected
backend hooks:

- `doAsyncConnect(...)`
- `doAsyncAccept(...)`
- `doAsyncSend(...)`
- `doAsyncReceive(...)`

The Windows and Linux backend declarations/definitions were also renamed to
these `doAsyncXxx` names:

- `WinIOCPNetworkInterface::{doAsyncConnect, doAsyncAccept, doAsyncSend, doAsyncReceive}`
- `LinuxAsyncNetworkInterface::{doAsyncConnect, doAsyncAccept, doAsyncSend, doAsyncReceive}`

Comments/log messages may still mention `asyncXxx`; those are not urgent unless
they cause confusion.

### New PacketReceiver Support Files

These files have been added:

- `src/myiperf/ParsedPacket.h`
- `src/myiperf/PacketStreamParser.h`
- `src/myiperf/PacketStreamParser.cpp`
- `src/myiperf/PacketReceiveStats.h`
- `src/myiperf/PacketReceiveStats.cpp`
- `src/myiperf/PacketDispatcher.h`
- `src/myiperf/PacketDispatcher.cpp`

`src/myiperf/PacketReceiver.h` and `src/myiperf/PacketReceiver.cpp` have already
been partially replaced with the new design. Current intended API:

```cpp
void start(ControlMessageBus& messages);
void stop();
TestStats getStats() const;
void resetStats();
```

Current intended members include:

```cpp
NetworkInterface* networkInterface;
std::atomic<bool> running;
size_t packetBufferSize;
PacketStreamParser parser;
PacketReceiveStats stats;
std::unique_ptr<PacketDispatcher> dispatcher;
Task receiverTask{nullptr};
```

## Remaining Required Work

Complete these steps before attempting functional testing.

### 1. Update ControlChannel

`ControlChannel.h/.cpp` still use the old callback-based signature:

```cpp
void attachReceiver(PacketReceiver& receiver,
                    ReceiverCompletionCallback onComplete);
```

Change it to:

```cpp
void attachReceiver(PacketReceiver& receiver);
```

Implementation should be:

```cpp
void ControlChannel::attachReceiver(PacketReceiver& receiver) {
  receiver.start(messages);
}
```

### 2. Update Session Calls

`ClientTestSession.cpp` and `ServerTestSession.cpp` still call
`attachReceiver(receiver, lambda)`.

Replace those calls with:

```cpp
context.control.attachReceiver(context.receiver);
```

Receiver-stop logging should remain inside `PacketReceiver::receiverLoop()` for
0-byte receive and exception paths.

### 3. Update CMakeLists.txt

`MyIperf/CMakeLists.txt` does not yet include the new files.

Add to `MYIPERF_CORE_PRIVATE_HEADERS`:

```cmake
src/myiperf/ParsedPacket.h
src/myiperf/PacketStreamParser.h
src/myiperf/PacketReceiveStats.h
src/myiperf/PacketDispatcher.h
```

Add to `MYIPERF_CORE_SOURCES`:

```cmake
src/myiperf/PacketStreamParser.cpp
src/myiperf/PacketReceiveStats.cpp
src/myiperf/PacketDispatcher.cpp
```

### 4. Fix Includes If Needed

Build errors may require small include additions:

- `PacketReceiver.cpp`: maybe `<exception>` and `<memory>`.
- `PacketStreamParser.cpp`: maybe `<string>`.
- `PacketReceiveStats.cpp`: maybe `<string>`.

Only add includes that the compiler actually needs.

### 5. Update PacketReceiver Comments

`PacketReceiver.h` comments still mention callback dispatch. Update comments to
reflect the new responsibilities:

- receive coroutine lifecycle: `PacketReceiver`
- parsing: `PacketStreamParser`
- stats: `PacketReceiveStats`
- message routing: `PacketDispatcher`

## Important Behavior To Preserve

- Do not send `DATA_PACKET` to `ControlMessageBus`.
- `PacketDispatcher` should call `stats.onDataPacket(packet)` for `DATA_PACKET`
  and then `continue`.
- For control messages, `PacketDispatcher` should call:

```cpp
messages.deliver(packet.header, packet.payload);
```

- `ControlMessageBus::deliver()` must continue resuming coroutines outside its
  mutex lock.
- `PacketReceiver::stop()` should only set `running=false`; socket close/cancel
  remains part of `NetworkInterface::close()`.
- Parser checksum failures should be counted by calling
  `PacketReceiveStats::onChecksumFailure()` once per returned checksum failure.

## Known Compile Breaks Likely Present Now

The code likely does not compile at this checkpoint because:

- `ControlChannel.h` references removed `ReceiverCompletionCallback`.
- `ControlChannel.cpp` calls removed `PacketReceiver::start(PacketCallback, ReceiverCompletionCallback)`.
- `ClientTestSession.cpp` and `ServerTestSession.cpp` call old
  `attachReceiver(receiver, lambda)`.
- `CMakeLists.txt` does not compile/link the new `.cpp` files.

## Suggested Continuation Order

1. Run `git status --short`.
2. Update `ControlChannel.h/.cpp`.
3. Update `ClientTestSession.cpp` and `ServerTestSession.cpp`.
4. Update `CMakeLists.txt`.
5. Search for old receiver callback API:

```powershell
rg -n "ReceiverCompletionCallback|PacketCallback" .\MyIperf\include .\MyIperf\src
```

Expected: no matches.

6. Search for public old async API:

```powershell
rg -n "virtual void asyncConnect|virtual void asyncAccept|virtual void asyncSend|virtual void asyncReceive" .\MyIperf\include .\MyIperf\src
```

Expected: no matches.

7. Confirm backend hooks:

```powershell
rg -n "doAsyncConnect|doAsyncAccept|doAsyncSend|doAsyncReceive" .\MyIperf\include .\MyIperf\src
```

Expected: matches only in `NetworkInterface` protected hooks and platform
backend implementations.

8. Build.

## Build Commands

Windows PowerShell:

```powershell
cmake -S .\MyIperf -B .\MyIperf\build
cmake --build .\MyIperf\build --config Debug
```

If configure/build uses a different local build directory, use the existing
project convention instead.

## Manual Functional Test Scenarios

After the build passes, verify:

- Server accepts client connection.
- `CONFIG_HANDSHAKE` / `CONFIG_ACK` exchange succeeds.
- Phase 1 client-to-server data, `TEST_FIN`, and stats exchange succeed.
- Phase 2 server-to-client data, final `STATS_ACK`, and `SHUTDOWN_ACK` succeed.
- Peer disconnect or 0-byte receive stops `PacketReceiver` without leaving
  session coroutines stuck.

## Git Status Warning

`git status --short` currently shows many unrelated deletes/moves/untracked
files outside this refactor. Do not use broad revert/reset commands. Work only
with the paths related to this refactor unless the user explicitly asks for
cleanup.

Directly relevant paths:

- `MyIperf/include/myiperf/NetworkInterface.h`
- `MyIperf/src/myiperf/platform/WinIOCPNetworkInterface.*`
- `MyIperf/src/myiperf/platform/LinuxAsyncNetworkInterface.*`
- `MyIperf/src/myiperf/PacketReceiver.*`
- `MyIperf/src/myiperf/ParsedPacket.h`
- `MyIperf/src/myiperf/PacketStreamParser.*`
- `MyIperf/src/myiperf/PacketReceiveStats.*`
- `MyIperf/src/myiperf/PacketDispatcher.*`
- `MyIperf/src/myiperf/ControlChannel.*`
- `MyIperf/src/myiperf/ClientTestSession.cpp`
- `MyIperf/src/myiperf/ServerTestSession.cpp`
- `MyIperf/CMakeLists.txt`
