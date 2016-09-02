#include "SphynxCommon.h"


//-----------------------------------------------------------------------------
// WindowedTimes

void WindowedTimes::Reset()
{
    Locker locker(StateLock);

    for (int i = 0; i < 4; ++i)
    {
        BestRing[i] = Sample();
    }
}

void WindowedTimes::Insert(u64 remoteSendMsec, u64 localRecvMsec)
{
    Locker locker(StateLock);

    u64 delta = localRecvMsec - remoteSendMsec;
    Sample* sample = BestRing + RingWriteIndex;

    u64 firstMsec = sample->FirstMsec;
    if (firstMsec == 0)
    {
        sample->FirstMsec = localRecvMsec;
        sample->LocalRecvMsec = localRecvMsec;
        sample->RemoteSendMsec = remoteSendMsec;
        return;
    }

    if (localRecvMsec - firstMsec >= kWinMsec)
    {
        static_assert(kWinCount == 2, "Need to change index update code");
        RingWriteIndex ^= 1;

        sample = BestRing + RingWriteIndex;
        sample->FirstMsec = localRecvMsec;
        sample->LocalRecvMsec = localRecvMsec;
        sample->RemoteSendMsec = remoteSendMsec;
        return;
    }

    u64 oldBestDelta = sample->LocalRecvMsec - sample->RemoteSendMsec;
    if ((s64)oldBestDelta - (s64)delta >= 0)
    {
        sample->LocalRecvMsec = localRecvMsec;
        sample->RemoteSendMsec = remoteSendMsec;
    }
}

u64 WindowedTimes::ComputeDelta(u64 nowMsec)
{
    Locker locker(StateLock);

    Sample* sample = BestRing + RingWriteIndex;
    if (sample->FirstMsec == 0)
        return 0;
    u64 delta = sample->LocalRecvMsec - sample->RemoteSendMsec;

    static_assert(kWinCount == 2, "Need to change index update code");
    sample = BestRing + (RingWriteIndex ^ 1);
    // Note: Signed conversion is intentional here to handle wrap-around properly
    if (sample->FirstMsec == 0 || (s64)(nowMsec - sample->LocalRecvMsec) > (s64)kBackLimitMsec)
        return delta;

    u64 delta2 = sample->LocalRecvMsec - sample->RemoteSendMsec;
    if ((s64)delta - (s64)delta2 >= 0)
        delta = delta2;

    return delta;
}


//-----------------------------------------------------------------------------
// WindowedTimes

bool IgnoreUnreachable(std::shared_ptr<asio::ip::udp::socket>& s, bool ignore)
{
    // FALSE = Disable behavior where, after receiving an ICMP Unreachable message,
    // WSARecvFrom() will fail.  Disables ICMP completely; normally this is good.
    // But when you're writing a client endpoint, you probably want to listen to
    // ICMP Port Unreachable or other failures until you get the first packet.
    // After that call IgnoreUnreachable() to avoid spoofed ICMP exploits.

#ifdef _WIN32
    DWORD behavior = ignore ? FALSE : TRUE;

    if (ioctlsocket(s->native_handle(), SIO_UDP_CONNRESET, &behavior) == SOCKET_ERROR)
    {
        LOG(WARNING) << "IgnoreUnreachable::ioctlsocket() failed: " << ::WSAGetLastError();
        DEBUG_BREAK;
        return false;
    }
#else
    // TODO
#endif

    return true;
}

bool DontFragment(std::shared_ptr<asio::ip::udp::socket>& s, bool df)
{
#ifdef _WIN32
    DWORD behavior = df ? TRUE : FALSE;

    // Useful to measure MTU
    if (setsockopt(s->native_handle(), IPPROTO_IP, IP_DONTFRAGMENT, (const char *)&behavior, sizeof(behavior)))
    {
        LOG(WARNING) << "IgnoreUnreachable::ioctlsocket() failed: " << ::WSAGetLastError();
        DEBUG_BREAK;
        return false;
    }
#else
    // TODO
#endif

    return true;
}


//-----------------------------------------------------------------------------
// Encryptor

void Encryptor::EncryptTCP(const u8* src, u8* dest, int bytes)
{
    u8 last = OutgoingTCPEncState.LastByte;
    const u8 adder = (u8)(OutgoingTCPEncState.Key >> 9);

    for (int i = 0; i < bytes; ++i)
    {
        u8 cur = src[i];
        dest[i] = (cur ^ last) - adder;
        last = cur;
    }

    OutgoingTCPEncState.LastByte = last;
}

void Encryptor::DecryptTCP(const u8* src, u8* dest, int bytes)
{
    u8 last = IncomingTCPEncState.LastByte;
    const u8 adder = (u8)(IncomingTCPEncState.Key >> 9);

    for (int i = 0; i < bytes; ++i)
        dest[i] = last = (src[i] + adder) ^ last;

    IncomingTCPEncState.LastByte = last;
}

void Encryptor::EncryptUDP(const u8* src, u8* dest, int bytes)
{
    u8 last = (u8)OutgoingUDPEncState.Key;
    const u8 adder = (u8)(OutgoingUDPEncState.Key >> 8);

    for (int i = 0; i < bytes; ++i)
    {
        u8 cur = src[i];
        dest[i] = (cur + last) ^ adder;
        last = cur;
    }
}

void Encryptor::DecryptUDP(const u8* src, u8* dest, int bytes)
{
    u8 last = (u8)IncomingUDPEncState.Key;
    const u8 adder = (u8)(IncomingUDPEncState.Key >> 8);

    for (int i = 0; i < bytes; ++i)
        dest[i] = last = (src[i] ^ adder) - last;
}

void Encryptor::InitializeEncryption(u32 key, EncryptionRole role)
{
    u32 incomingKey = key;
    u32 outgoingKey = key ^ 0x12345678;

    // If in server role:
    if (role == EncryptionRole::Server)
    {
        // Swap
        u32 temp = incomingKey;
        incomingKey = outgoingKey;
        outgoingKey = temp;
    }

    OutgoingTCPEncState.Key = outgoingKey;
    OutgoingTCPEncState.LastByte = (u8)(outgoingKey >> 20);
    IncomingTCPEncState.Key = incomingKey;
    IncomingTCPEncState.LastByte = (u8)(incomingKey >> 20);
    OutgoingUDPEncState.Key = ~outgoingKey;
    IncomingUDPEncState.Key = ~incomingKey;
}


//-----------------------------------------------------------------------------
// SphynxPeer

SphynxPeer::SphynxPeer()
	: UDPCallSender([this](Stream& stream) { PackUDP(stream); })
	, TCPCallSender([this](Stream& stream) { PackTCP(stream); })
{
	IsFullConnection = false;
	Disconnected = false;

    UDPOutBufferSize = kPackingBufferSizeBytes;
    UDPOutBuffer = std::make_unique<u8[]>(UDPOutBufferSize);

    TCPOutBufferSize = kPackingBufferSizeBytes;
    TCPOutBuffer = std::make_unique<u8[]>(TCPOutBufferSize);

    DecompressedBufferSize = ZBUFF_recommendedDOutSize();
    DecompressedBuffer = std::make_unique<u8[]>(DecompressedBufferSize);

    CompressionBufferSize = ZBUFF_recommendedCOutSize();
    CompressionBuffer = std::make_unique<u8[]>(CompressionBufferSize);

    DecompressionContext = ZBUFF_createDCtx();
    ZBUFF_decompressInit(DecompressionContext);

    CompressionContext = ZBUFF_createCCtx();
}

SphynxPeer::~SphynxPeer()
{
    if (CompressionContext)
    {
        ZBUFF_freeCCtx(CompressionContext);
        CompressionContext = nullptr;
    }
    if (DecompressionContext)
    {
        ZBUFF_freeDCtx(DecompressionContext);
        DecompressionContext = nullptr;
    }
}

void SphynxPeer::Start(std::shared_ptr<asio::io_context>& context)
{
	Context = context;

	TCPSocket = std::make_shared<asio::ip::tcp::socket>(*Context);
}

void SphynxPeer::Stop()
{
	LOG(DEBUG) << "Stopping TCP connection";

	TCPSocket->close();
	TCPSocket = nullptr;
}

void SphynxPeer::SendUDP(const u8* data, int bytes)
{
	if (bytes <= 0)
	{
		DEBUG_BREAK; return;
	}
	uint8_t* packet = new (std::nothrow) uint8_t[bytes];
	if (!packet)
	{
		DEBUG_BREAK; return;
	}

    Cipher.EncryptUDP(data, packet, bytes);

    UDPSocket->async_send_to(asio::buffer(packet, bytes), PeerUDPAddress,
		[packet, this](const asio::error_code& error, std::size_t sentBytes)
	{
		delete[] packet;
		if (!!error)
			OnUDPSendError(error);
	});
}

void SphynxPeer::OnUDPSendError(const asio::error_code& error)
{
    LOG(WARNING) << "UDP send error: " << error.message();
}

void SphynxPeer::OnTCPRead(Stream& wholePacket)
{
    u8* data = (u8*)wholePacket.GetFront();
    int dataSize = (int)wholePacket.GetRemaining();
    Cipher.DecryptTCP(data, data, dataSize);

    while (dataSize > 0)
	{
        size_t srcsz = dataSize, destsz = DecompressedBufferSize;

		size_t zr = ZBUFF_decompressContinue(DecompressionContext, &DecompressedBuffer[0], &destsz, data, &srcsz);

        if (ZBUFF_isError(zr))
        {
            LOG(WARNING) << "Invalid compressed data, err=" << ZSTD_getErrorName(zr) << " #" << zr;
            Disconnect();
            DEBUG_BREAK; return;
        }
        if (zr != 0 || destsz <= 0)
        {
            LOG(WARNING) << "Invalid compressed data, zr=" << zr << ", destsz=" << destsz;
            Disconnect();
            DEBUG_BREAK; return;
        }

        data += srcsz;
        dataSize -= (int)srcsz;

		Stream stream;
		stream.WrapRead(&DecompressedBuffer[0], destsz);
		OnTCPData(stream);

        ZBUFF_decompressInit(DecompressionContext);
    }
}

void SphynxPeer::PackTCP(Stream& stream)
{
	Locker locker(TCPFlushLock);
	if (TCPOutUsed + stream.GetUsed() > TCPOutBufferSize)
	{
		FlushUDP();
		FlushTCP();
	}
	memcpy(&TCPOutBuffer[0] + TCPOutUsed, stream.GetFront(), stream.GetUsed());
	TCPOutUsed += stream.GetUsed();
}

void SphynxPeer::PackUDP(Stream& stream)
{
	Locker locker(UDPFlushLock);
	if (UDPOutUsed + stream.GetUsed() > UDPOutBufferSize)
		FlushUDP();
	memcpy(&UDPOutBuffer[0] + UDPOutUsed, stream.GetFront(), stream.GetUsed());
	UDPOutUsed += stream.GetUsed();
}

void SphynxPeer::Flush()
{
	FlushUDP();
	FlushTCP();
}

void SphynxPeer::Disconnect()
{
	Disconnected = true;
}

bool SphynxPeer::IsDisconnected() const
{
	return Disconnected;
}

void SphynxPeer::FlushTCP()
{
	Locker locker(TCPFlushLock);

	u8* data = &TCPOutBuffer[0];
	size_t bytes = TCPOutUsed;
	if (bytes <= 0)
		return;

	TCPOutUsed = 0;
	size_t destlen = 0;

    ZBUFF_compressInit(CompressionContext, kCompressionLevel);

	for (;;)
	{
		destlen = CompressionBufferSize;

		size_t used = bytes;
		size_t cr = ZBUFF_compressContinue(CompressionContext, &CompressionBuffer[0], &destlen, data, &used);

		if (destlen < 0 || ZBUFF_isError(cr) || used <= 0 || used > bytes)
		{
            LOG(WARNING) << "Invalid send compressed data, err=" << ZSTD_getErrorName(cr) << " #" << cr;
			DEBUG_BREAK; return;
		}

		if (used == bytes)
			break;
        if (destlen != 0)
        {
            LOG(WARNING) << "Compressor did not use all data but also did not generate output";
            DEBUG_BREAK; return;
        }

        SendTCP(&CompressionBuffer[0], (int)destlen);

		data += used;
		bytes -= (int)used;
	}

	size_t offset = destlen;
	for (;;)
	{
		size_t remaining = (size_t)CompressionBufferSize - offset;
		size_t written = remaining;
        size_t cr = ZBUFF_compressEnd(CompressionContext, &CompressionBuffer[0] + offset, &written);

		if (ZBUFF_isError(cr))
		{
            LOG(WARNING) << "Invalid send end compressed data, err=" << ZSTD_getErrorName(cr) << " #" << cr;
			DEBUG_BREAK; return;
		}

		offset += written;

        SendTCP(&CompressionBuffer[0], (int)offset);

		if (cr == 0)
			break;

		offset = 0;
	}
}

void SphynxPeer::SendTCP(const u8* data, int bytes)
{
	if (bytes <= 0)
	{
		DEBUG_BREAK; return;
	}
	uint8_t* packet = new (std::nothrow) uint8_t[bytes];
	if (!packet)
	{
		DEBUG_BREAK; return;
	}

    Cipher.EncryptTCP(data, packet, bytes);

    TCPSocket->async_send(asio::buffer(packet, bytes),
		[packet, this](const asio::error_code& error, std::size_t sentBytes)
	{
		delete[] packet;
		if (!!error)
			OnTCPSendError(error);
	});
}

void SphynxPeer::FlushUDP()
{
	Locker locker(UDPFlushLock);

	u8* data = &UDPOutBuffer[0];
	int bytes = (int)UDPOutUsed;

	UDPOutUsed = 2;

	if (bytes <= 2)
		return;

	*(u16*)data = (u16)GetTimeMsec();

	SendUDP(data, bytes);
}

void SphynxPeer::OnTCPData(Stream& stream)
{
	RouteData(stream);
}

void SphynxPeer::OnUDPData(u64 nowMsec, Stream& rawStream)
{
    u8* data = rawStream.GetFront();
    int dataSize = rawStream.GetBufferSize();
    Cipher.DecryptUDP(data, data, dataSize);

    Stream stream;
    stream.WrapRead(data, dataSize);

	u16 partialTime;
	if (!stream.Serialize(partialTime))
		return;

	if (RouteData(stream))
	{
		LastReceiveLocalMsec = nowMsec;

		// Only use timestamps if the rest of the data is not invalid
		u64 sentTime = ReconstructCounter16(LastUDPReceiveRemoteMsec, partialTime);
		LastUDPReceiveRemoteMsec = sentTime;
		WinTimes.Insert(sentTime, nowMsec);
	}
}

bool SphynxPeer::RouteData(Stream& stream)
{
    bool success = false;

    while (Router.Call(stream))
        success = true;

	return success;
}

void SphynxPeer::PostNextTCPRead()
{
	auto mutableBuffer = TCPReceiveStreamBuf.prepare(kTCPRecvLimitBytes);

	TCPSocket->async_read_some(mutableBuffer, [this](
		const asio::error_code& error, std::size_t bytes_transferred)
	{
		if (!!error)
			OnTCPReadError(error);
		else if (bytes_transferred <= 0)
			OnTCPClose();
		else
		{
            Stream packet;
            packet.WrapRead(TCPReceiveStreamBuf.data().data(), bytes_transferred);

            OnTCPRead(packet);

            TCPReceiveStreamBuf.consume(bytes_transferred);

            PostNextTCPRead();
		}
	});
}

void SphynxPeer::OnTCPReadError(const asio::error_code& error)
{
    LOG(WARNING) << "TCP read error: ", error.message();
	Disconnect();
}

void SphynxPeer::OnTCPSendError(const asio::error_code& error)
{
    LOG(WARNING) << "TCP send error: ", error.message();
	Disconnect();
}

void SphynxPeer::OnTCPClose()
{
	LOG(INFO) << "TCP close";
	Disconnect();
}


//-----------------------------------------------------------------------------
// Logging

#ifdef _ANDROID
    #include <android/log.h>
#else
    #include <iostream>
#endif

struct CustomSink
{
    void log(g3::LogMessageMover message)
    {
        std::string str = message.get().toString();
#ifdef _ANDROID
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", str.c_str());
#else
        std::cout << str;
#endif
#ifdef _WIN32
        ::OutputDebugStringA(str.c_str());
#endif
    }
};

void InitializeLogging()
{
    // Setup logging
    std::unique_ptr<g3::LogWorker> logworker = g3::LogWorker::createLogWorker();
    //logworker->addDefaultLogger("client", "");
    auto sinkHandle = logworker->addSink(std::make_unique<CustomSink>(),
        &CustomSink::log);
    g3::initializeLogging(logworker.get());
}
