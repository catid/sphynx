#pragma once

#include "asio/include/asio.hpp"
#include "Tools.h"
#include <memory>
#include <thread>
#include "g3log/g3log.hpp"
#include "g3log/logworker.hpp"
#include "Stream.h"
#include "RPC.h"
#include "zstd/lib/zstd.h"
#include "zstd/lib/common/zbuff.h"


//-----------------------------------------------------------------------------
// Constants

// Kernel buffer sizes for UDP/TCP ports
static const int kUDPSendBufferSizeBytes = 64000;
static const int kUDPRecvBufferSizeBytes = 64000;
static const int kTCPSendBufferSizeBytes = 64000;
static const int kTCPRecvBufferSizeBytes = 64000;

// Number of bytes to read at a time
static const int kTCPRecvLimitBytes = 16000;

// Server timer interval
static const int kServerWorkerTimerIntervalMsec = 30; // msec

// Client timer interval
static const int kClientWorkerTimerIntervalMsec = 100; // msec

// Interval between heartbeats
static const int kS2CTCPHeartbeatIntervalMsec = 10000; // 10 seconds
static const int kS2CUDPTimeSyncIntervalFastMsec = 300; // 300 ms
static const int kS2CUDPTimeSyncIntervalSlowMsec = 1000; // 1 second
static const int kS2CUDPTimeSyncFastCount = 10; // 10 count

// Timeouts
static const int kS2CTimeoutMsec = 40000; // 40 seconds
static const int kC2STimeoutMsec = 40000; // 40 seconds

// UDP datagram max size
static const int kUDPDatagramMax = 490;

// Time between client sending UDP handshakes
static const int kClientHandshakeIntervalMsec = 100; // msec

// Packing buffer sizes
static const int kPackingBufferSizeBytes = kUDPDatagramMax; // in bytes

// Compression level to use for TCP packet compression
static const int kCompressionLevel = 9;


//-----------------------------------------------------------------------------
// S2C Protocol

typedef void S2CHeartbeatT();
static const int S2CHeartbeatID = 255;

typedef void S2CTimeSyncT(u16 bestC2Sdelta);
static const int S2CTimeSyncID = 254;

typedef void S2CTCPHandshakeT(u32 cookie, u16 udpPort);
static const int S2CTCPHandshakeID = 253;


//-----------------------------------------------------------------------------
// C2S Protocol

typedef void C2SUDPHandshakeT(u32 cookie);
static const int C2SUDPHandshakeID = 255;

typedef void C2SHeartbeatT(u16 sendTime);
static const int C2SHeartbeatID = 254;


//-----------------------------------------------------------------------------
// Sockets

bool IgnoreUnreachable(std::shared_ptr<asio::ip::udp::socket>& s, bool ignore);
bool DontFragment(std::shared_ptr<asio::ip::udp::socket>& s, bool df);


//-----------------------------------------------------------------------------
// WindowedTimes

class WindowedTimes
{
public:
    void Reset();
    void Insert(u64 remoteSendMsec, u64 localRecvMsec);
    u64 ComputeDelta(u64 nowMsec);

protected:
    Lock StateLock;

    struct Sample
    {
        u64 FirstMsec = 0;
        u64 RemoteSendMsec = 0;
        u64 LocalRecvMsec = 0;
    };

    static const int kWinCount = 2;
    static const u64 kWinMsec = 20 * 1000; // 20s
    static const u64 kBackLimitMsec = kWinMsec * kWinCount;

    Sample BestRing[kWinCount] = {};
    int RingWriteIndex = 0;
};


//-----------------------------------------------------------------------------
// Encryption

struct TCPEncryptionState
{
    u32 Key = 0;
    u8 LastByte = 0x21;
};

struct UDPEncryptionState
{
    u32 Key = 0;
};

enum class EncryptionRole
{
    Server,
    Client
};

class Encryptor
{
public:
    void InitializeEncryption(u32 key, EncryptionRole role);

    void EncryptTCP(const u8* src, u8* dest, int bytes);
    void DecryptTCP(const u8* src, u8* dest, int bytes);
    void EncryptUDP(const u8* src, u8* dest, int bytes);
    void DecryptUDP(const u8* src, u8* dest, int bytes);

private:
    UDPEncryptionState OutgoingUDPEncState, IncomingUDPEncState;
    TCPEncryptionState OutgoingTCPEncState, IncomingTCPEncState;
};


//-----------------------------------------------------------------------------
// SphynxPeer
//
// Shared code between SphynxServer::Connection and SphynxClient

class SphynxPeer
{
public:
	SphynxPeer();
	virtual ~SphynxPeer();

	void Start(std::shared_ptr<asio::io_context>& context);
	void Stop();

	void Flush();
	void Disconnect();
	bool IsDisconnected() const;

	// Router for incoming calls
	CallRouter Router;

	// Set CallSerializer::CallSender to one of these
	const std::function<void(Stream&)> UDPCallSender;
	const std::function<void(Stream&)> TCPCallSender;

protected:
    void OnTCPRead(Stream& stream);
	void OnTCPData(Stream& stream);
	void PostNextTCPRead();
	void OnTCPReadError(const asio::error_code& error);
	void OnTCPSendError(const asio::error_code& error);
	void OnTCPClose();
	void SendTCP(const u8* data, int bytes);

	void OnUDPData(u64 nowMsec, Stream& stream);
    void SendUDP(const u8* data, int bytes);
	void OnUDPSendError(const asio::error_code& error);

	void PackTCP(Stream& stream);
	void PackUDP(Stream& stream);
	void FlushTCP();
	void FlushUDP();

	bool RouteData(Stream& stream);

    Encryptor Cipher;

	// Asio context
	std::shared_ptr<asio::io_context> Context;

	// TCP and UDP socket for connection
	std::shared_ptr<asio::ip::tcp::socket> TCPSocket;
	std::shared_ptr<asio::ip::udp::socket> UDPSocket;

	// Peer's UDP address if IsFullConnection is true
	std::atomic_bool IsFullConnection;
	asio::ip::udp::endpoint PeerUDPAddress;

	// IsDisconnected() flag
	std::atomic_bool Disconnected;

	// Last UDP or TCP packet local receive time for timeouts
	u64 LastReceiveLocalMsec = 0;

	// Last UDP packet expanded remote timestamp
	u64 LastUDPReceiveRemoteMsec = 0;

	// UDP time synchronization data collection
	WindowedTimes WinTimes;

	// TCP receive stream buffer
	asio::streambuf TCPReceiveStreamBuf;

	// Outgoing UDP datagram buffer
	Lock UDPFlushLock;
	std::unique_ptr<u8[]> UDPOutBuffer;
	size_t UDPOutUsed = 2;
	size_t UDPOutBufferSize = 0;

	// Outgoing TCP datagram buffer
	Lock TCPFlushLock;
    std::unique_ptr<u8[]> TCPOutBuffer;
	size_t TCPOutUsed = 0;
	size_t TCPOutBufferSize = 0;

	// TCP decompression context and buffer
	ZBUFF_DCtx* DecompressionContext = nullptr;
    std::unique_ptr<u8[]> DecompressedBuffer;
	size_t DecompressedBufferSize = 0;

	// TCP compression context and buffer
	ZBUFF_CCtx* CompressionContext = nullptr;
    std::unique_ptr<u8[]> CompressionBuffer;
	size_t CompressionBufferSize = 0;
};
