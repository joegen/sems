/*
 * Copyright (C) 2002-2003 Fhg Fokus
 *
 * This file is part of SEMS, a free SIP media server.
 *
 * SEMS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. This program is released under
 * the GPL with the additional exemption that compiling, linking,
 * and/or using OpenSSL is allowed.
 *
 * For a license to use the SEMS software under conditions
 * other than those described here, or to purchase support for this
 * software, please contact iptel.org by e-mail at the following addresses:
 *    info@iptel.org
 *
 * SEMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/** @file AmRtpStream.h */
#ifndef _RtpStream_h_
#define _RtpStream_h_


#include "AmSdp.h"
#include "AmThread.h"
#include "SampleArray.h"
#include "AmRtpPacket.h"
#include "AmEvent.h"
#include "AmDtmfSender.h"

#include <netinet/in.h>

#include <string>
#include <map>
#include <queue>
#include <memory>
using std::string;
using std::auto_ptr;
using std::pair;

// return values of AmRtpStream::receive
#define RTP_EMPTY        0 // no rtp packet available
#define RTP_PARSE_ERROR -1 // error while parsing rtp packet
#define RTP_TIMEOUT     -2 // last received packet is too old
#define RTP_DTMF        -3 // dtmf packet has been received
#define RTP_BUFFER_SIZE -4 // buffer overrun
#define RTP_UNKNOWN_PL  -5 // unknown payload


/**
 * Forward declarations
 */
class  AmAudio;
class  AmSession;
struct SdpPayload;
struct amci_payload_t;

/**
 * This provides the memory for the receive buffer.
 */
struct PacketMem {
#define MAX_PACKETS 32

  AmRtpPacket packets[MAX_PACKETS];
  bool        used[MAX_PACKETS];

  PacketMem();

  inline AmRtpPacket* newPacket();
  inline void freePacket(AmRtpPacket* p);
  inline void clear();
};

/** \brief event fired on RTP timeout */
class AmRtpTimeoutEvent
  : public AmEvent
{
	
public:
  AmRtpTimeoutEvent() 
    : AmEvent(0) { }
  ~AmRtpTimeoutEvent() { }
};


/**
 * \brief RTP implementation
 *
 * Rtp stream high level interface.
 */
class AmRtpStream
  : public AmObject
{
protected:

  // payload collection
  struct Payload {
    unsigned char pt;
    string        name;
    unsigned int  clock_rate;
    int           codec_id;
  };

  struct PayloadMapping {
    // remote payload type
    int8_t remote_pt;

    // index in payloads vector
    uint8_t index;
  };

  typedef std::map<unsigned int, AmRtpPacket*, ts_less> ReceiveBuffer;
  typedef std::queue<AmRtpPacket*>                      RtpEventQueue;
  typedef std::vector<Payload>                          PayloadCollection;
  typedef std::map<unsigned char, PayloadMapping>       PayloadMappingTable;
  
  // list of locally supported payloads
  PayloadCollection payloads;

  // current payload (index into @payloads)
  int payload;

  // mapping from local payload type to PayloadMapping
  PayloadMappingTable pl_map;

  /** SDP media slot number (n-th media line) */
  int sdp_media_index;

  /** RTP sequence number */
  unsigned int sequence;

  /**
     Payload of last received packet.
     Usefull to detect talk spurt, looking 
     for comfort noise packets.
  */
  int         last_payload;

  /** Remote host information */
  string             r_host;
  unsigned short     r_port;

  /** 
   * Local interface used for this stream
   * (index into @AmConfig::Ifs)
   */
  int l_if;

  /**
   * Local and remote host addresses
   */
#ifdef SUPPORT_IPV6
  struct sockaddr_storage r_saddr;
  struct sockaddr_storage l_saddr;
#else
  struct sockaddr_in r_saddr;
  struct sockaddr_in l_saddr;
#endif

  /** Local port */
  unsigned short     l_port;

  /** Local socket */
  int                l_sd;

  /** Timestamp of the last received RTP packet */
  struct timeval last_recv_time;

  /** Local and remote SSRC information */
  unsigned int   l_ssrc;
  unsigned int   r_ssrc;
  bool           r_ssrc_i;

  /** symmetric RTP */
  bool           passive;      // passive mode ?

  /** mute && port == 0 */
  bool           hold;

  /** marker flag */
  bool           begin_talk;

  /** do check rtp timeout */
  bool           monitor_rtp_timeout;

  /** Payload type for telephone event */
  auto_ptr<const SdpPayload> remote_telephone_event_pt;
  auto_ptr<const SdpPayload> local_telephone_event_pt;

  /** DTMF sender */
  AmDtmfSender   dtmf_sender;

  /**
   * Receive buffer, queue and mutex
   */
  PacketMem       mem;
  ReceiveBuffer   receive_buf;
  RtpEventQueue   rtp_ev_qu;
  AmMutex         receive_mut;

  /** if relay_stream is initialized, received RTP is relayed there */
  bool            relay_enabled;
  /** pointer to relay stream.
      NOTE: This may only be accessed in initialization
      or by the AmRtpReceiver thread while relaying!  */
  AmRtpStream*    relay_stream;
  /** control transparency for RTP seqno in RTP relay mode */
  bool            relay_transparent_seqno;
  /** control transparency for RTP ssrc in RTP relay mode */
  bool            relay_transparent_ssrc;

  /** Session owning this stream */
  AmSession*         session;

  /** Payload provider */
  AmPayloadProvider* payload_provider;

  // get the next available port within configured range
  static int getNextPort();

  /** Insert an RTP packet to the buffer queue */
  void bufferPacket(AmRtpPacket* p);
  /* Get next packet from the buffer queue */
  int nextPacket(AmRtpPacket*& p);
  
  /** handle symmetric RTP - if in passive mode, update raddr from rp */
  void handleSymmetricRtp(AmRtpPacket* rp);

  void relay(AmRtpPacket* p);

  /** Sets generic parameters on SDP media */
  void getSdp(SdpMedia& m);

  /** Clear RTP timeout at time recv_time */
  void clearRTPTimeout(struct timeval* recv_time);

  /** checks if this packet is to be relayed or not */
  bool isPacketToRelay(AmRtpPacket *p);

public:

  /** Mute */
  bool mute;

  /** should we receive packets? if not -> drop */
  bool receiving;

  /** Allocates resources for future use of RTP. */
  AmRtpStream(AmSession* _s, int _if);

  /** Stops the stream and frees all resources. */
  virtual ~AmRtpStream();

  int send( unsigned int ts,
	    unsigned char* buffer,
	    unsigned int   size );
  
  int send_raw( char* packet, unsigned int length );

  int compile_and_send( const int payload, bool marker, 
		        unsigned int ts, unsigned char* buffer, 
		        unsigned int size );

  int receive( unsigned char* buffer, unsigned int size,
	       unsigned int& ts, int& payload );

  void recvPacket();

  /** ping the remote side, to open NATs and enable symmetric RTP */
  int ping();

  /** returns the socket descriptor for local socket (initialized or not) */
  int hasLocalSocket();

  /** initializes and gets the socket descriptor for local socket */
  int getLocalSocket();

  /**
   * This function must be called before setLocalPort, because
   * setLocalPort will bind the socket and it will be not
   * possible to change the IP later
   */
  void setLocalIP(const string& ip);
	    
  /** Initializes a new random local port, and sets own attributes properly. */
  void setLocalPort();

  /** 
   * Gets RTP port number. If no RTP port in assigned, assigns a new one.
   * @return local RTP port. 
   */
  int getLocalPort();

  /** 
   * Gets remote RTP port.
   * @return remote RTP port.
   */
  int getRPort();
    
  /**
   * Gets remote host IP.
   * @return remote host IP.
   */
  string getRHost();

  /**
   * Set remote IP & port.
   */
  void setRAddr(const string& addr, unsigned short port);

  /** Symmetric RTP: passive mode ? */
  void setPassiveMode(bool p);
  bool getPassiveMode() { return passive; }

  unsigned int get_ssrc() { return l_ssrc; }

  int getLocalTelephoneEventRate();

  void setPayloadProvider(AmPayloadProvider* pl_prov);

  int getSdpMediaIndex() { return sdp_media_index; }
  int getPayloadType() { return payload; }

  /**
   * send a DTMF as RTP payload (RFC4733)
   * @param event event ID (e.g. key press), see rfc
   * @param duration_ms duration in milliseconds
   */
  void sendDtmf(int event, unsigned int duration_ms);

  /**
   * Generate an SDP offer based on the stream capabilities.
   * @param index index of the SDP media within the SDP.
   * @param offer the local offer to be filled/completed.
   */
  virtual void getSdpOffer(unsigned int index, SdpMedia& offer);

  /**
   * Generate an answer for the given SDP media based on the stream capabilities.
   * @param index index of the SDP media within the SDP.
   * @param offer the remote offer.
   * @param answer the local answer to be filled/completed.
   */
  virtual void getSdpAnswer(unsigned int index, const SdpMedia& offer, SdpMedia& answer);

  /**
   * Enables RTP stream.
   * @param local the SDP message generated by the local UA.
   * @param remote the SDP message generated by the remote UA.
   * @warning It is necessary to call getSdpOffer/getSdpAnswer prior to init(...)
   * @warning so that the internal SDP media line index is set properly.
   */
  virtual int init(const AmSdp& local, const AmSdp& remote);

  /**
   * Stops RTP stream.
   */
  void pause();

  /**
   * Resume a paused RTP stream.
   */
  void resume();

  /** set the RTP stream on hold (mute && port = 0) */
  void setOnHold(bool on_hold);
  
  /** get the RTP stream on hold  */
  bool getOnHold();

  /** setter for monitor_rtp_timeout */
  void setMonitorRTPTimeout(bool m) { monitor_rtp_timeout = m; }
  /** getter for monitor_rtp_timeout */
  bool getMonitorRTPTimeout() { return monitor_rtp_timeout; }

  /*
   * clear RTP timeout to current time
   */
  void clearRTPTimeout();

  /** set relay stream for  RTP relaying */
  void setRelayStream(AmRtpStream* stream);

  /** ensable RTP relaying through relay stream */
  void enableRtpRelay();

  /** disable RTP relaying through relay stream */
  void disableRtpRelay();

  /** enable or disable transparent RTP seqno for relay */
  void setRtpRelayTransparentSeqno(bool transparent);

  /** enable or disable transparent SSRC seqno for relay */
  void setRtpRelayTransparentSSRC(bool transparent);
};

#endif

// Local Variables:
// mode:C++
// End:

