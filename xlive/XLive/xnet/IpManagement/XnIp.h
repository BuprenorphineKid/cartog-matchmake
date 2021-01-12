#pragma once 

#include "..\xnet.h"
#include "..\Sockets\XSocket.h"

#define MAX_HDR_STR 32

const char requestStrHdr[MAX_HDR_STR] = "XNetBrOadPack";
const char broadcastStrHdr[MAX_HDR_STR] = "XNetReqPack";

#define XnIp_ConnectionIndexMask 0xFF000000

#define XnIp_ConnectionPing 0x0
#define XnIp_ConnectionPong 0x2
#define XnIp_ConnectionCloseSecure 0x4
#define XnIp_ConnectionEstablishSecure 0x8

#define XnIp_ConnectionTimeOut (15 * 1000) // msec

#define IPADDR_LOOPBACK htonl(0x7F000001) // 127.0.0.1

struct XNetPacketHeader
{
	DWORD intHdr;
	char HdrStr[MAX_HDR_STR];
};

struct XBroadcastPacket
{
	XBroadcastPacket::XBroadcastPacket()
	{
		pckHeader.intHdr = 'BrOd';
		strncpy(pckHeader.HdrStr, broadcastStrHdr, MAX_HDR_STR);
		ZeroMemory(&data, sizeof(XBroadcastPacket::XBroadcast));
		data.name.sin_addr.s_addr = INADDR_BROADCAST;
	};

	XNetPacketHeader pckHeader;
	struct XBroadcast
	{
		sockaddr_in name;
	};
	XBroadcast data;
};

struct XNetRequestPacket
{
	XNetRequestPacket()
	{
		pckHeader.intHdr = 'XNeT';
		memset(pckHeader.HdrStr, 0, sizeof(pckHeader.HdrStr));
		strncpy(pckHeader.HdrStr, requestStrHdr, MAX_HDR_STR);
	}

	XNetPacketHeader pckHeader;
	struct XNetReq
	{
		XNKID xnkid;
		DWORD reqType;
		union
		{
			XNADDR xnaddr;
		};
	};
	XNetReq data;
};

struct XnKeyPair
{
	bool bValid;
	XNKID xnkid;
	XNKEY xnkey;
};

struct XnIp
{
	IN_ADDR connectionIdentifier;
	XNADDR xnaddr;

	XnKeyPair* keyPair;
	bool bValid;
	int xnetstatus;
	int connectionPacketsSentCount;
	DWORD lastConnectionInteractionTime;

	// NAT info
	sockaddr_in NatAddrSocket1000; // TODO: allocate dynamically based on how many sockets are up
	sockaddr_in NatAddrSocket1001;

	unsigned int pckSent;
	unsigned int pckRecvd;

	unsigned int bytesSent;
	unsigned int bytesRecvd;

	IN_ADDR getOnlineIpAddress()
	{
		return xnaddr.inaOnline;
	}

	IN_ADDR getLanIpAddr()
	{
		return xnaddr.ina;
	}

	bool XnIp::isValid(IN_ADDR identifier)
	{
		if (identifier.s_addr != connectionIdentifier.s_addr)
		{
			LOG_CRITICAL_NETWORK("{} - {:X} != {:X}", __FUNCTION__, identifier.s_addr, connectionIdentifier.s_addr);
			return false;
		}

		return bValid && identifier.s_addr == connectionIdentifier.s_addr;;
	}
};

class CXnIp
{
public:

	/*
		Use this to get the connection index from an XLive API call
	*/
	static int CXnIp::getConnectionIndex(IN_ADDR connectionIdentifier)
	{
		return ntohl(connectionIdentifier.s_addr & XnIp_ConnectionIndexMask);
	}

	XnIp* CXnIp::getConnection(IN_ADDR ina)
	{
		XnIp* ret = &XnIPs[getConnectionIndex(ina)];
		if (ret->isValid(ina))
			return ret;
		else
			return nullptr;
	}

	void CXnIp::setTimeConnectionInteractionHappened(IN_ADDR ina)
	{
		XnIp* xnIp = getConnection(ina);
		if (xnIp != nullptr)
			xnIp->lastConnectionInteractionTime = timeGetTime();
	}

	int CreateXnIpIdentifier(const XNADDR* pxna, const XNKID* xnkid, IN_ADDR* outIpIdentifier, bool handleFromConnectionPacket);
	void UnregisterXnIpIdentifier(const IN_ADDR ina);

	void checkForLostConnections();

	void SetupLocalConnectionInfo(unsigned long xnaddr, unsigned long lanaddr, const char* abEnet, const char* abOnline);

	void CXnIp::UnregisterLocalConnectionInfo()
	{
		SecureZeroMemory(&ipLocal, sizeof(ipLocal));
	}

	XnIp* CXnIp::GetLocalUserXn()
	{
		if (ipLocal.bValid)
			return &ipLocal;

		return nullptr;
	}

	int handleRecvdPacket(XSocket* xsocket, sockaddr_in* lpFrom, WSABUF* lpBuffers, LPDWORD bytesRecvdCount);

	void Initialize(const XNetStartupParams* netStartupParams);
	IN_ADDR GetConnectionIdentifierByRecvAddr(XSocket* xsocket, sockaddr_in* addr);
	void SaveConnectionNatInfo(XSocket* xsocket, IN_ADDR ipIdentifier, sockaddr_in* addr);
	void HandleConnectionPacket(XSocket* xsocket, XNetRequestPacket* connectReqPkt, sockaddr_in* addr, LPDWORD bytesRecvdCount);

	void HandleDisconnectPacket(XSocket* xsocket, XNetRequestPacket* disconnectReqPck, sockaddr_in* recvAddr);
	int RegisterKey(XNKID*, XNKEY*);
	XnKeyPair* getKeyPair(const XNKID* xnkid);
	void UnregisterKey(const XNKID* xnkid);
	void getLastRegisteredKeys(XNKID* xnkid, XNKEY* xnkey);
	
	XnIp* XnIPs = nullptr;
	XnKeyPair* XnKeyPairs = nullptr;

	XNetStartupParams startupParams;
	int GetMaxXnConnections() { return startupParams.cfgSecRegMax; }
	int GetReqQoSBufferSize() { return startupParams.cfgQosDataLimitDiv4 * 4; }
	int GetMaxXnKeyPairs() { return startupParams.cfgKeyRegMax; }

	int GetMinSockRecvBufferSizeInBytes() { return startupParams.cfgSockDefaultRecvBufsizeInK * SOCK_K_UNIT; }
	int GetMinSockSendBufferSizeInBytes() { return startupParams.cfgSockDefaultSendBufsizeInK * SOCK_K_UNIT; }


private:

	XnIp ipLocal;
	XnKeyPair* lastRegisteredKey = nullptr;
};

extern CXnIp gXnIp;
