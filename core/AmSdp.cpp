/*
 *parse or be parsed
 */


#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "AmConfig.h"
#include "AmSdp.h"
#include "AmUtils.h"
#include "AmPlugIn.h"
#include "AmSession.h"
#include "ErrorSdp.h"

#include "amci/amci.h"
#include "log.h"

using std::string;
using namespace std;

static bool parse_sdp_line_ex(AmSdp* sdp_msg, char*& s);
static void parse_sdp_connection(AmSdp* sdp_msg, char* s, char t);
static void parse_sdp_media(AmSdp* sdp_msg, char* s);
static void parse_sdp_attr(AmSdp* sdp_msg, char* s);
static void parse_sdp_origin(AmSdp* sdp_masg, char* s);

inline char* get_next_line(char* s);
static char* is_eql_next(char* s);
static char* parse_until(char* s, char end);
static bool contains(char* s, char* next_line, char c);

enum parse_st {SDP_DESCR, SDP_MEDIA};
enum sdp_connection_st {NET_TYPE, ADDR_TYPE, IP4, IP6};
enum sdp_media_st {MEDIA, PORT, PROTO, FMT}; 
enum sdp_attr_rtpmap_st {TYPE, ENC_NAME, CLK_RATE, ENC_PARAM};
enum sdp_attr_fmtp_st {FORMAT, FORMAT_PARAM};
enum sdp_origin_st {USER, ID, VERSION_ST, NETTYPE, ADDR, UNICAST_ADDR};

// inline functions

inline string net_t_2_str(int nt)
{
  switch(nt){
  case NT_IN: return "IN";
  default: return "<unknown network type>";
  }
}

inline string addr_t_2_str(int at)
{
  switch(at){
  case AT_V4: return "IP4";
  case AT_V6: return "IP6";
  default: return "<unknown address type>";
  }
}


inline string media_t_2_str(int mt)
{
  switch(mt){
  case MT_AUDIO: return "audio";
  case MT_VIDEO: return "video";
  case MT_APPLICATION: return "application";
  case MT_TEXT: return "text";
  case MT_MESSAGE: return "message";
  default: return "<unknown media type>";
  }
}

inline string transport_p_2_str(int tp)
{
  switch(tp){
  case TP_RTPAVP: return "RTP/AVP";
  case TP_UDP: return "udp";
  case TP_RTPSAVP: return "RTP/SAVP";
  default: return "<unknown media type>";
  }
}

bool SdpPayload::operator == (int r)
{
  DBG("pl == r: payload_type = %i; r = %i\n", payload_type, r);
  return payload_type == r;
}

//
// class AmSdp: Methods
//
AmSdp::AmSdp()
  : remote_active(false),
    telephone_event_pt(NULL)
{
}

AmSdp::AmSdp(const AmSdp& p_sdp_msg)
  : version(p_sdp_msg.version),
    origin(p_sdp_msg.origin),
    sessionName(p_sdp_msg.sessionName),
    conn(p_sdp_msg.conn),
    media(p_sdp_msg.media),
    telephone_event_pt(NULL),
    remote_active(false)
{
  memcpy(r_buf,p_sdp_msg.r_buf,BUFFER_SIZE);
}


void AmSdp::setBody(const char* _sdp_msg)
{
  if (!memchr(_sdp_msg, '\0', BUFFER_SIZE)) {
    throw AmSession::Exception(513, "Message too big");
  }
  strcpy(r_buf, _sdp_msg);
}

int AmSdp::parse()
{
  char* s = r_buf;
  media.clear();
  
  bool ret = parse_sdp_line_ex(this,s);
  
  if(!ret && conn.address.empty()){
    for(vector<SdpMedia>::iterator it = media.begin();
	!ret && (it != media.end()); ++it)
      ret = it->conn.address.empty();
    
    if(ret){
      ERROR("A connection field must be field must be present in every\n");
      ERROR("media description or at the session level.\n");
    }
  }
  
  telephone_event_pt = findPayload("telephone-event");
  
  return ret;
  
}

int AmSdp::genResponse(const string& localip, int localport, string& out_buf, bool single_codec)
{
  string l_ip = "IP4" + localip;
  
#ifdef SUPPORT_IPV6
  if(localip.find('.') == string::npos)
    l_ip = "IP6" + localip;
#endif
  
  out_buf =
  "v=0\r\n"
  "o=username 0 0 IN " + l_ip + "\r\n"
  "s=session\r\n"
  "c=IN " + l_ip + "\r\n"
  "t=0 0\r\n"
  "m=audio " + int2str(localport) + " RTP/AVP";
 
 string payloads;
 string options;
 
 for(vector<SdpPayload*>::iterator it = sup_pl.begin();
     it != sup_pl.end(); ++it){
   payloads += " " + int2str((*it)->payload_type);
   if((*it)->encoding_param == 0){
     options += "a=rtpmap:" + int2str((*it)->payload_type) + " "
       + (*it)->encoding_name + "/" + int2str((*it)->clock_rate) + "\r\n";
   }else{
     options += "a=rtpmap:" + int2str((*it)->payload_type) + " "
       + (*it)->encoding_name + "/" + int2str((*it)->clock_rate) + "/" + int2str((*it)->encoding_param) + "\r\n";

   }

   if((*it)->sdp_format_parameters.size()){
     options += "a=fmtp:" + int2str((*it)->payload_type) + " "
       + (*it)->sdp_format_parameters + "\r\n";
   }
   if (single_codec) break;
 }
 
 if (hasTelephoneEvent())
   payloads += " " + int2str(telephone_event_pt->payload_type);
 
 out_buf += payloads + "\r\n"
  +options;
 
 if(hasTelephoneEvent())
   {
     out_buf += "a=rtpmap:" + int2str(telephone_event_pt->payload_type) + " " + 
       telephone_event_pt->encoding_name + "/" +
       int2str(telephone_event_pt->clock_rate) + "\r\n"
       "a=fmtp:" + int2str(telephone_event_pt->payload_type) + "0-15\r\n";
   }
 
 if(remote_active /* dir == SdpMedia::DirActive */)
   out_buf += "a=direction:passive\r\n";
 
 return 0;
}

int AmSdp::genRequest(const string& localip, int localport, string& out_buf)
{
  AmPlugIn* plugin = AmPlugIn::instance();
  const map<int, amci_payload_t*>& payloads = plugin->getPayloads();
  const map<int,int>& payload_order = plugin->getPayloadOrder();

  if(payloads.empty()){
    ERROR("no payload plugin loaded.\n");
    return -1;
  }

  string l_ip = "IP4 " + localip;

#ifdef SUPPORT_IPV4
  if(localip.find('.') == string::npos)
    l_ip= "IP6 " + localip;
#endif

  out_buf = 
    "v=0\r\n"
    "o=username 0 0 IN " + l_ip + "\r\n"
    "s=session\r\n"
    "c=IN " + l_ip + "\r\n"
    "t=0 0\r\n"
    "m=audio " + int2str(localport) + " RTP/AVP ";

  map<int,int>::const_iterator it = payload_order.begin();
  out_buf += int2str((it++)->second);

  for(; it != payload_order.end(); ++it)
    out_buf += string(" ") + int2str(it->second);

  out_buf += "\r\n";
  
  for (it = payload_order.begin(); it != payload_order.end(); ++it) {
    map<int,amci_payload_t*>::const_iterator it2 = payloads.find(it->second);
    if(it2 != payloads.end()){
      out_buf += "a=rtpmap:" + int2str(it2->first)
	+ " " + string(it2->second->name)
	+ "/" + int2str(it2->second->sample_rate)
	+ "\r\n";
    } else {
      ERROR("Payload %d was not found in payloads map!\n", it->second);
      return -1;
    }
  }
  return 0;

}

const vector<SdpPayload*>& AmSdp::getCompatiblePayloads(int media_type, string& addr, int& port)
{
  vector<SdpMedia>::iterator m_it;
  SdpPayload *payload;
  sup_pl.clear();

  AmPlugIn* pi = AmPlugIn::instance();
  for(m_it = media.begin(); m_it != media.end(); ++m_it){
    //DBG("type found: %d\n", m_it->type);
    //DBG("media port: %d\n", m_it->port);
    //DBG("media transport: %d\n", m_it->transport);
    //    for(int i=0; i < 8 ; i++){
    //  DBG("media payloads: %d\n", m_it->payloads[i].payload_type);
    //  DBG("media payloads: %s\n", m_it->payloads[i].encoding_name.c_str());
    //  DBG("media clock rates: %d\n", m_it->payloads[i].clock_rate);
    //}
    //    DBG("type found: %d\n", m_it->payloads[0].t);
    if( (media_type != m_it->type) )
      continue;

    vector<SdpPayload>::iterator it = m_it->payloads.begin();
    for(; it!= m_it->payloads.end(); ++it) {
           amci_payload_t* a_pl = NULL;
      if(it->payload_type < DYNAMIC_PAYLOAD_TYPE_START) {
	// try static payloads
	a_pl = pi->payload(it->payload_type);
      }

      if( a_pl) {
	payload  = &(*it);
	payload->int_pt = a_pl->payload_id;
	payload->encoding_name = a_pl->name;
	payload->clock_rate = a_pl->sample_rate;
	sup_pl.push_back(payload);
      }
      else {
	// Try dynamic payloads
	// and give a chance to broken
	// implementation using a static payload number
	// for dynamic ones.
	if(it->encoding_name == "telephone-event")
	  continue;

	int int_pt = getDynPayload(it->encoding_name,
				   it->clock_rate);
	if(int_pt != -1){
	  payload = &(*it);
	  payload->int_pt = int_pt;
	  sup_pl.push_back(payload);
	}
      }
    }
    if( sup_pl.size() > 0)
    {
      if(m_it->conn.address.empty())
	{
	  DBG("using global address: %s\n", m_it->conn.address.c_str());
	  addr = conn.address;
	}
      else {
	DBG("using media specific address: %s\n", m_it->conn.address.c_str());
	addr = m_it->conn.address;
      }
      
      if(m_it->dir == SdpMedia::DirActive)
	remote_active = true;
      
      port = (int)m_it->port;
    }
    break;
  }
  return sup_pl;
}
	
bool AmSdp::hasTelephoneEvent()
{
  return telephone_event_pt != NULL;
}

int AmSdp::getDynPayload(const string& name, int rate)
{
  AmPlugIn* pi = AmPlugIn::instance();
  const map<int, amci_payload_t*>& ref_payloads = pi->getPayloads();

  for(map<int, amci_payload_t*>::const_iterator pl_it = ref_payloads.begin();
      pl_it != ref_payloads.end(); ++pl_it)
    if( (name == pl_it->second->name)
	&& (rate == pl_it->second->sample_rate) )
      return pl_it->first;

  return -1;
}

const SdpPayload *AmSdp::findPayload(const string& name)
{
  vector<SdpMedia>::iterator m_it;

  for (m_it = media.begin(); m_it != media.end(); ++m_it)
    {
      vector<SdpPayload>::iterator it = m_it->payloads.begin();
      for(; it != m_it->payloads.end(); ++it)

	{
	  if (it->encoding_name == name)
	    {
	      return new SdpPayload(*it);
	    }
	}
    }
  return NULL;
}

//parser
static bool parse_sdp_line_ex(AmSdp* sdp_msg, char*& s)
{

  char* next=0;
  register parse_st state;
  //default state
  state=SDP_DESCR;
  DBG("parse_sdp_line_ex: parsing sdp messages .....\n%s\n", s);

  while(*s != '\0'){
    switch(state){
    case SDP_DESCR:
      switch(*s){
      case 'v':
	{
	  s = is_eql_next(s);
	  next = get_next_line(s);
	  string version(s, int(next-s)-2);
	  str2i(version, sdp_msg->version);
	  DBG("parse_sdp_line_ex: found version\n");
	  s = next;
	  state = SDP_DESCR;
	  break;
	  
	}
      case 'o':
	DBG("parse_sdp_line_ex: found origin\n");
	s = is_eql_next(s);
	parse_sdp_origin(sdp_msg, s);
	s = get_next_line(s);
	state = SDP_DESCR;
	break;
      case 's':
	{
	  DBG("parse_sdp_line_ex: found session\n");
	  s = is_eql_next(s);
	  next = get_next_line(s);
	  string sessionName(s, int(next-s)-2);
	  sdp_msg->sessionName = sessionName;
	  s = next;
	  break;
	}
      case 'i':
      case 'u':
      case 'e':
      case 'p':
      case 'b':
      case 't':
      case 'k':
	DBG("parse_sdp_line_ex: found some random characters\n");
	s = is_eql_next(s);
	next = get_next_line(s);
	s = next;
	state = SDP_DESCR;
	break;
      case 'a':
	  DBG("parse_sdp_line_ex: found attributes\n");
	s = is_eql_next(s);
	next = get_next_line(s);
	//	parse_sdp_attr(sdp_msg, s);
	s = next;
	state = SDP_DESCR;
	break;
      case 'c':
	DBG("parse_sdp_line_ex: found connection\n");
	s = is_eql_next(s);
	parse_sdp_connection(sdp_msg, s, 'd');
	s = get_next_line(s);
	state = SDP_DESCR;	
	break;
      case 'm':
	DBG("parse_sdp_line_ex: found media\n");
	state = SDP_MEDIA;	
	break;

      default:
	{
	  next = get_next_line(s);
	  string line(s, int(next-s)-1);
	  DBG("parse_sdp_line: skipping unknown Session description %s=\n", (char*)line.c_str());
	  s = next;
	  break;
	}
      }
      break;

    case SDP_MEDIA:
      switch(*s){
      case 'm':
	s = is_eql_next(s);
	parse_sdp_media(sdp_msg, s);
	s = get_next_line(s);
	state = SDP_MEDIA;
	break;
      case 'i':
	s = is_eql_next(s);
	s = get_next_line(s);
	state = SDP_MEDIA;
	break;
      case 'c':
	s = is_eql_next(s);
	DBG("parse_sdp_line: found media connection\n");
	parse_sdp_connection(sdp_msg, s, 'm');
	s = get_next_line(s);
	state = SDP_MEDIA;
	break;
      case 'b':
	s = is_eql_next(s);
	s = get_next_line(s);
	state = SDP_MEDIA;
	break;
      case 'k':
	s = is_eql_next(s);
	s = get_next_line(s);
	state = SDP_MEDIA;
	break;
      case 'a':
	s = is_eql_next(s);
	DBG("parse_sdp_line: found media attr\n");
	parse_sdp_attr(sdp_msg, s);
	s = get_next_line(s);
	state = SDP_MEDIA;
	break;
	
      default :
	{
	  next = get_next_line(s);
	  string line(s, int(next-s)-1);
	  DBG("parse_sdp_line: skipping unknown Media description %s=\n", (char*)line.c_str());
	  s = next;
	  break;
	}
      }
      break;
    }
  }
  DBG("parse_sdp_line_ex: parsing sdp message done :) \n");
  return false;
}


static void parse_sdp_connection(AmSdp* sdp_msg, char* s, char t)
{
  
  char* connection_line=s;
  char* next=0;
  char* line_end=0;
  int parsing=1;

  SdpConnection c;

  line_end = get_next_line(s);
  register sdp_connection_st state;
  state = NET_TYPE;

  DBG("parse_sdp_line_ex: parse_sdp_connection: parsing sdp connection\n");

  while(parsing){
    switch(state){
    case NET_TYPE:
      //Ignore NET_TYPE since it is always IN 
      connection_line +=3;
      state = ADDR_TYPE;
      break;
    case ADDR_TYPE:
      {
	string addr_type(connection_line,3);
	connection_line +=4;
	if(addr_type == "IP4"){
	  c.addrType = 1;
	  state = IP4;
	}else if(addr_type == "IP6"){
	  c.addrType = 2;
	  state = IP6;
	}else{
	  ERROR("parse_sdp_connection: Unknow addr_type in c=\n");
	  c.addrType = 0;
	  parsing = 0;
	}
	break;
      }
    case IP4:
      {
	if(contains(connection_line, line_end, '/')){
	  next = parse_until(s, '/');
	  string ip4(connection_line,int(next-connection_line)-1);
	  c.address = ip4;
	  char* s = (char*)ip4.c_str();
	  inet_aton(s, &c.ipv4.sin_addr);
	}else{
	  string ip4(connection_line, int(line_end-connection_line)-1);
	  c.address = ip4;
	  char* s = (char*)ip4.c_str();
	  inet_aton(s, &c.ipv4.sin_addr);
	}
	parsing = 0;
	break;
      }
      
    case IP6:
      { 
	if(contains(connection_line, line_end, '/')){
	  next = parse_until(s, '/');
	  string ip6(connection_line, int(next-connection_line)-1);
	  c.address = ip6;
	  char* s = (char*)ip6.c_str();
	  inet_pton(AF_INET6, s, &c.ipv6.sin6_addr);
	}else{
	  string ip6(connection_line, int(line_end-connection_line)-1);
	  c.address = ip6;
	  char* s = (char*)ip6.c_str();
	  inet_pton(AF_INET6, s, &c.ipv6.sin6_addr);
	}
	parsing = 0;
	break;
      }
    }
  }
  if(t == 'd')
    sdp_msg->conn = c;
  if(t == 'm'){
    SdpMedia& media = sdp_msg->media.back();
    media.conn = c;
  }

  DBG("parse_sdp_line_ex: parse_sdp_connection: done parsing sdp connection\n");
  return;
}


static void parse_sdp_media(AmSdp* sdp_msg, char* s)
{
  SdpMedia m;
  
  //clear(m);

  register sdp_media_st state;
  state = MEDIA;
  int parsing = 1;
  char* media_line=s;
  char* next=0;
  char* line_end=0;
  line_end = get_next_line(media_line);
  SdpPayload payload;
  unsigned int payload_type;
  DBG("parse_sdp_line_ex: parse_sdp_media: parsing media description, %s \n", s);
  m.dir = SdpMedia::DirBoth;

  while(parsing){
    switch(state){
    case MEDIA: 
      {
      next = parse_until(media_line, ' ');
      string media(media_line, int(next-media_line)-1);
      if(media_type(media) < 0 )
	ERROR("parse_sdp_media: Unknow media type\n");
      m.type = media_type(media);
      media_line = next;
      state = PORT;
      break;
      }
    case PORT:
      {
      next = parse_until(media_line, ' ');
      //check for multiple ports
      if(contains(media_line, next, '/')){
	//port number
	next = parse_until(media_line, '/');
	string port(media_line, int(next-media_line)-1);
	str2i(port, m.port);	
	//number of ports
	media_line = next;
	next = parse_until(media_line, ' ');
       	string nports(media_line, int(next-media_line)-1);
	str2i(nports, m.nports);
      }else{
	//port number 
	next = parse_until(media_line, ' ');
	const string port(media_line, int(next-media_line)-1);
	str2i(port, m.port);
	media_line = next;
      }
      state = PROTO;
      break;
      }
    case PROTO:
      {
	next = parse_until(media_line, ' ');
	string proto(media_line, int(next-media_line)-1);
	if(transport_type(proto) < 0){
	  ERROR("parse_sdp_media: Unknow transport protocol\n");
	  state = FMT;
	  break;
	}
	m.transport = transport_type(proto);
	media_line = next;
	state = FMT;
	break;
      }
    case FMT:
      {
	if(contains(media_line, line_end, ' ')){
	next = parse_until(media_line, ' ');
	//if(next < line_end){
	  string value(media_line, int(next-media_line)-1);
	  media_line = next;
	  payload.type = m.type;
	  str2i(value, payload_type);
	  payload.payload_type = payload_type;
	  m.payloads.push_back(payload);
	  state = FMT;
	  //check if this lines is also the last
	}else if (*(line_end-1) == '\0'){
	  string last_value(media_line, int(line_end-media_line)-1);
	  payload.type = m.type;
	  str2i(last_value, payload_type);
	  payload.payload_type = payload_type;
	  m.payloads.push_back(payload);
	  parsing = 0;
	  //if not
	}else{
	  //check if it should be -1 or -2
	  string last_value(media_line, int(line_end-media_line)-1);
	  payload.type = m.type;
	  str2i(last_value, payload_type);
	  payload.payload_type = payload_type;
	  m.payloads.push_back(payload);
	  parsing=0;
	}
	break;
      }
    }
  }
  sdp_msg->media.push_back(m);

  DBG("parse_sdp_line_ex: parse_sdp_media: done parsing media description \n");
  return;
}

static void parse_sdp_attr(AmSdp* sdp_msg, char* s)
{
 
  DBG("parse_sdp_line_ex: parse_sdp_attr.......\n");
  if(sdp_msg->media.empty()){
    ERROR("While parsing media options: no actual media !\n");
    return;
  }
  
  SdpMedia& media = sdp_msg->media.back();

  SdpPayload payload;

  register sdp_attr_rtpmap_st rtpmap_st;
  register sdp_attr_fmtp_st fmtp_st;
  rtpmap_st = TYPE;
  fmtp_st = FORMAT;
  char* attr_line=s;
  char* next=0;
  char* line_end=0;
  int parsing = 1;
  line_end = get_next_line(attr_line);
  
  unsigned int payload_type, clock_rate, encoding_param;
  string encoding_name, params;

  if(contains(attr_line, line_end, ':')){
    next = parse_until(attr_line, ':');
    string attr(attr_line, int(next-attr_line)-1);
    attr_line = next;
    if(attr == "rtpmap"){
      while(parsing){
	switch(rtpmap_st){
	case TYPE:
	  {
	    next = parse_until(attr_line, ' ');
	    string type(attr_line, int(next-attr_line)-1);
	    str2i(type,payload_type);
	    attr_line = next;
	    rtpmap_st = ENC_NAME;
	    break;
	  }
	case ENC_NAME:
	  {
	    if(contains(s, line_end, '/')){
	      next = parse_until(attr_line, '/');
	      string enc_name(attr_line, int(next-attr_line)-1);
	      encoding_name = enc_name;
	      attr_line = next;
	      rtpmap_st = CLK_RATE;
	      break;
	    }else{
	      rtpmap_st = ENC_PARAM;
	      break;
	    }
	  }
	case CLK_RATE:
	  {
	    // check for posible encoding parameters after clock rate
	    if(contains(attr_line, line_end, '/')){
	      next = parse_until(attr_line, '/');
	      string clk_rate(attr_line, int(next-attr_line)-1);
	      str2i(clk_rate, clock_rate);
	      attr_line = next;
	      rtpmap_st = ENC_PARAM;
	      //last line check
	    }else if (*(line_end-1) == '\0') {
	      string clk_rate(attr_line, int(line_end-attr_line)-1);
	      str2i(clk_rate, clock_rate);
	      parsing = 0;
	      //more lines to come
	    }else{
	      string clk_rate(attr_line, int(line_end-attr_line)-1);
	      str2i(clk_rate, clock_rate);
	      parsing=0;
	    }
	    
	    break;
	  }
	case ENC_PARAM:
	  {
	    next = parse_until(attr_line, ' ');
	    if(next < line_end){
	      string value(attr_line, int(next-attr_line)-1);
	      str2i(value, encoding_param);
	      attr_line = next;
	      params += value;
	      params += ' ';
	      rtpmap_st = ENC_PARAM;
	    }else{
	      string last_value(attr_line, int(line_end-attr_line)-1);
	      str2i(last_value, encoding_param);
	      params += last_value;
	      parsing = 0;
	    }
	    break;
	  }
	  break;
	}
      }
      
      vector<SdpPayload>::iterator pl_it;
      
      for( pl_it=media.payloads.begin();
	   (pl_it != media.payloads.end())
	     && (pl_it->payload_type != int(payload_type));
	   ++pl_it);

      if(pl_it != media.payloads.end()){
	*pl_it = SdpPayload( int(payload_type),
			     encoding_name,
			     int(clock_rate),
			     int(encoding_param));
      }
      

    }else if(attr == "fmtp"){
      while(parsing){
	switch(fmtp_st){
	case FORMAT:
	  {
	    next = parse_until(attr_line, ' ');
	    string fmtp_format(attr_line, int(next-attr_line)-1);
	    str2i(fmtp_format, payload_type);
	    attr_line = next;
	    fmtp_st = FORMAT_PARAM;
	    break;
	  }
	case FORMAT_PARAM:
	  { 
	    next = parse_until(attr_line, ' ');
	    if(next < line_end){
	      string value(attr_line, int(next-attr_line)-1);
	      attr_line = next;
	      params += value;
	      params += ' ';
	      fmtp_st = FORMAT_PARAM;
	    }else{
	      string last_value(attr_line, int(line_end-attr_line)-1);
	      params += last_value;
	      parsing = 0;
	    }
	    break;
	      }
	  break;
	}
      }
      //  payload.type = media.type;
      //payload.payload_type = payload_type;
      //payload.sdp_format_parameters = params;

      
    }else{
      attr_check(attr);
      attr_line = next;
      string value(attr_line, int(line_end-attr_line)-1);
      //payload.type = media.type;
      //payload.encoding_name = attr;
      //payload.sdp_format_parameters = value;
      
    }
  }else{
    string attr(attr_line, int(line_end-attr_line)-1);
    attr_check(attr);
    //payload.type = media.type;
    //payload.encoding_name = attr;
    
  }

  media.payloads.push_back(payload);

  DBG("parse_sdp_line_ex: parse_sdp_attr: done parsing sdp attributes\n");
  return;
}

static void parse_sdp_origin(AmSdp* sdp_msg, char* s)
{
  char* origin_line = s;
  char* next=0;
  char* line_end=0;
  line_end = get_next_line(s);
  
  register sdp_origin_st origin_st;
  origin_st = USER;
  int parsing = 1;
  
  SdpOrigin origin;

  DBG("parse_sdp_line_ex: parse_sdp_origin: parsing sdp origin\n");

  while(parsing){
    switch(origin_st)
      {
      case USER:
	{
	  next = parse_until(origin_line, ' ');
	  if(next > line_end){
	    DBG("parse_sdp_origin: ST_USER: Incorrect number of value in o=\n");
	    origin_st = UNICAST_ADDR;
	    break;
	  }
	  string user(origin_line, int(next-origin_line)-1);
	  origin.user = user;
	  origin_line = next;
	  origin_st = ID;
	  break;
	}
      case ID:
	{
	  next = parse_until(origin_line, ' ');
	  if(next > line_end){
	    DBG("parse_sdp_origin: ST_ID: Incorrect number of value in o=\n");
	    origin_st = UNICAST_ADDR;
	    break;
	  }
	  string id(origin_line, int(next-origin_line)-1);
	  str2i(id, origin.sessId);
	  origin_line = next;
	  origin_st = VERSION_ST;
	  break;
	}
      case VERSION_ST:
	{
	  next = parse_until(origin_line, ' ');
	  if(next > line_end){
	    DBG("parse_sdp_origin: ST_VERSION: Incorrect number of value in o=\n");
	    origin_st = UNICAST_ADDR;
	    break;
	  }
	  string version(origin_line, int(next-origin_line)-1);
	  str2i(version, origin.sessV);
	  origin_line = next;
	  origin_st = NETTYPE;
	  break;
	}
      case NETTYPE:
	{
	  next = parse_until(origin_line, ' ');
	  if(next > line_end){
	    DBG("parse_sdp_origin: ST_NETTYPE: Incorrect number of value in o=\n");
	    origin_st = UNICAST_ADDR;
	    break;
	  }
	  string net_type(origin_line, int(next-origin_line)-1);
	  origin_line = next;
	  origin_st = ADDR;
	  break;
	}
      case ADDR:
	{
       	  next = parse_until(origin_line, ' ');
	  if(next > line_end){
	    DBG("parse_sdp_origin: ST_ADDR: Incorrect number of value in o=\n");
	    origin_st = UNICAST_ADDR;
	    break;
	  }
	  string addr_type(origin_line, int(next-origin_line)-1);
	  origin_line = next;
	  origin_st = UNICAST_ADDR;
	  break;
	}
      case UNICAST_ADDR:
	{
	  next = parse_until(origin_line, ' ');
	  //check if line contains more values than allowed
	  if(next > line_end){
	    string unicast_addr(origin_line, int(line_end-origin_line)-1);
	  }else{
	    DBG("parse_sdp_origin: 'o=' contains more values than allowed; these values will be ignored\n");  
	    string unicast_addr(origin_line, int(next-origin_line)-1);
	  }
	  parsing = 0;
	  break;
	}
      }
  }
  
  sdp_msg->origin = origin;

  DBG("parse_sdp_line_ex: parse_sdp_origin: done parsing sdp origin\n");
  return;
}


/*
 *HELPER FUNCTIONS
 */

static bool contains(char* s, char* next_line, char c)
{
  char* line=s;
  while(line != next_line-1){
    if(*line == c)
      return true;
    *line++;
  }
  return false;
}

static char* parse_until(char* s, char end)
{
  char* line=s;
  while(*line != end ){
    line++;
  }
  line++;
  return line;
}


static char* is_eql_next(char* s)
{
  char* current_line=s;
  if(*(++current_line) != '='){
    DBG("parse_sdp_line: expected '=' but found <%c> \n", *current_line);
  }
  current_line +=1;
  return current_line;
}

inline char* get_next_line(char* s)
{
  char* next_line=s;
  //search for next line
 while( *next_line != '\0') {
    if(*next_line == 13){
      next_line +=2;
      break;
    }
    else if(*next_line == 10){	
      next_line +=1;
      break;
    }  
    next_line++;
  }
  if(*next_line == '\0')
  next_line +=1;

  return next_line; 
}


