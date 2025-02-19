#include <arp.h>
#include <checksum.h>
#include <dhcp.h>
#include <malloc.h>
#include <socket.h>
#include <system.h>
#include <timer.h>
#include <udp.h>
#include <util.h>

// Dynamic Host Configuration Protocol, according to:
// https://en.wikipedia.org/wiki/Dynamic_Host_Configuration_Protocol
// Copyright (C) 2024 Panagiotis

uint8_t dhcpBroadcastMAC[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
uint8_t dhcpBroadcastIp[] = {0xff, 0xff, 0xff, 0xff};

void netDHCPapproveOptions(NIC *nic) {
  uint8_t     body[sizeof(dhcpHeader) + 128] = {0};
  dhcpHeader *header = (dhcpHeader *)body;
  memset(header, 0, sizeof(dhcpHeader));

  header->opcode = DHCP_SEND;
  header->htype = DHCP_TYPE_ETH;
  header->hlen = 0x06;
  header->hops = 0x00;

  header->xid = switch_endian_32(nic->dhcpTransactionID);

  header->secs = switch_endian_16(0x0);
  header->flags = switch_endian_16(0x0);

  memset(header->client_address, 0, 4);
  memcpy(header->your_ip, nic->ip, 4);
  memcpy(header->server_ip, nic->serverIp, 4);
  memset(header->gateway_ip, 0, 4);

  memcpy(header->client_mac, nic->MAC, 6);

  header->signature = switch_endian_32(DHCP_SIGNATURE);

  uint32_t extras = 0;

  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_MESSAGE_TYPE;
  body[sizeof(dhcpHeader) + (extras++)] = 1;
  body[sizeof(dhcpHeader) + (extras++)] = DHCP_REQUEST;

  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_REQUESTED_IP;
  body[sizeof(dhcpHeader) + (extras++)] = 4;
  body[sizeof(dhcpHeader) + (extras++)] = header->your_ip[0];
  body[sizeof(dhcpHeader) + (extras++)] = header->your_ip[1];
  body[sizeof(dhcpHeader) + (extras++)] = header->your_ip[2];
  body[sizeof(dhcpHeader) + (extras++)] = header->your_ip[3];

  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_SERVER_ID;
  body[sizeof(dhcpHeader) + (extras++)] = 4;
  body[sizeof(dhcpHeader) + (extras++)] = header->server_ip[0];
  body[sizeof(dhcpHeader) + (extras++)] = header->server_ip[1];
  body[sizeof(dhcpHeader) + (extras++)] = header->server_ip[2];
  body[sizeof(dhcpHeader) + (extras++)] = header->server_ip[3];

  body[sizeof(dhcpHeader) + (extras++)] = 0xff;

  netUdpSend(nic, dhcpBroadcastMAC, dhcpBroadcastIp, body,
             sizeof(dhcpHeader) + extras, 68, 67);
}

// returns true only on ack
bool netDHCPreceive(NIC *nic, void *body, uint32_t size) {
  udpHeader  *udp = (udpHeader *)((size_t)body + sizeof(netPacketHeader) +
                                 sizeof(IPv4header));
  dhcpHeader *dhcp = (dhcpHeader *)((size_t)udp + sizeof(udpHeader));
  uint8_t    *dhcpOptions = (uint8_t *)((size_t)dhcp + sizeof(dhcpHeader));

  if (switch_endian_32(dhcp->xid) != nic->dhcpTransactionID)
    return false;

  if (dhcp->opcode != DHCP_RECEIVE) {
    debugf("[networking::dhcp] Not-a-server! opcode{0x%02X}\n", dhcp->opcode);
    return false;
  }

  // the comment on dhcp.h explains this rather odd way of fetching options
  bool optionFetch = true;

  // scan n1; identify message/option type so we can proceed
  uint8_t dhcpMessageType = 0;
  while (optionFetch) { // scan n1
    switch (dhcpOptions[0]) {
    case DHCP_OPTION_MESSAGE_TYPE:
      dhcpMessageType = dhcpOptions[2];
      break;
    case DHCP_OPTION_END:
      optionFetch = false;
      break;
    default:
      break;
    }
    if (optionFetch)
      dhcpOptions = (uint8_t *)((size_t)dhcpOptions + 1 + 1 + dhcpOptions[1]);
    // type(1) + size(1) + rest(?)
  }

  switch (dhcpMessageType) {
  case DHCP_OFFER:
    memcpy(nic->ip, dhcp->your_ip, 4); // get our ip

    // scan n2; scan all fields now, knowing it's a DHCP "OFFER"
    optionFetch = true;
    dhcpOptions = (uint8_t *)((size_t)dhcp + sizeof(dhcpHeader));
    while (optionFetch) { // scan n2
      switch (dhcpOptions[0]) {
      case DHCP_OPTION_MESSAGE_TYPE:
        if (dhcpOptions[2] != dhcpMessageType)
          debugf("[networking::dhcp] Something incredibely fucked up is going "
                 "on: n2_scan_option{%02X} dhcpMessageType{%02X}\n",
                 dhcpOptions[2], dhcpMessageType);
        break;
      case DHCP_OPTION_ROUTER:
        memcpy(nic->serverIp, (void *)((size_t)dhcpOptions + 2), 4);
        break;
      case DHCP_OPTION_DNS_SERVER:
        memcpy(nic->dnsIp, (void *)((size_t)dhcpOptions + 2), 4);
        break;
      case DHCP_OPTION_SUBNET_MASK:
        memcpy(nic->subnetMask, (void *)((size_t)dhcpOptions + 2), 4);
        break;
      case DHCP_OPTION_LEASE_TIME:
        break;
      case DHCP_OPTION_END:
        optionFetch = false;
        break;
      }
      if (optionFetch)
        dhcpOptions = (uint8_t *)((size_t)dhcpOptions + 1 + 1 + dhcpOptions[1]);
      // type(1) + size(1) + rest(?)
    }

    // if options have no router ip, get it from header (if it has it)
    if (!(*((uint32_t *)nic->serverIp)) && *((uint32_t *)dhcp->server_ip))
      memcpy(nic->serverIp, dhcp->server_ip, 4);

    netDHCPapproveOptions(nic);
    break;
  case DHCP_ACK:
    // done...
    return true;
    break;
  default:
    debugf("[networking::dhcp] Odd DHCP message type! %d\n", dhcpMessageType);
    break;
  }

  return false;
}

void netDHCPinit(NIC *nic) {
  uint8_t     body[sizeof(dhcpHeader) + 128] = {0};
  dhcpHeader *header = (dhcpHeader *)body;
  memset(header, 0, sizeof(dhcpHeader));

  header->opcode = DHCP_SEND;
  header->htype = DHCP_TYPE_ETH;
  header->hlen = 0x06;
  header->hops = 0x00;

  header->xid = switch_endian_32(nic->dhcpTransactionID);

  header->secs = switch_endian_16(0x0);
  header->flags = switch_endian_16(0x0);

  memset(header->client_address, 0, 4);
  memset(header->your_ip, 0, 4);
  memset(header->server_ip, 0, 4);
  memset(header->gateway_ip, 0, 4);

  memcpy(header->client_mac, nic->MAC, 6);

  header->signature = switch_endian_32(DHCP_SIGNATURE);

  uint32_t extras = 0;

  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_MESSAGE_TYPE;
  body[sizeof(dhcpHeader) + (extras++)] = 1;
  body[sizeof(dhcpHeader) + (extras++)] = DHCP_DISCOVERY;

  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_REQUEST_PARAMETERS_LIST;
  body[sizeof(dhcpHeader) + (extras++)] = 4;
  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_ROUTER;
  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_DNS_SERVER;
  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_SUBNET_MASK;
  body[sizeof(dhcpHeader) + (extras++)] = DHCP_OPTION_LEASE_TIME;

  body[sizeof(dhcpHeader) + (extras++)] = 0xff;

  if (!nic->dhcpUdpRegistered) {
    nic->dhcpUdpRegistered = netSocketConnect(nic, SOCKET_PROT_UDP, 0, 68, 67);
  }
  netUdpSend(nic, dhcpBroadcastMAC, dhcpBroadcastIp, body,
             sizeof(dhcpHeader) + extras, 68, 67);

  uint64_t caputre = timerTicks;
  bool     dhcpRet = false;

  // when i trust kernel tasks enough, could just create one of those...
  // !*(uint32_t *)(&nic->ip[0])
  while (!dhcpRet && timerTicks < (caputre + DHCP_TIMEOUT)) {
    socketPacketHeader *head = netSocketRecv(nic->dhcpUdpRegistered);
    if (head) {
      dhcpRet = netDHCPreceive(
          nic, (void *)((size_t)head + sizeof(socketPacketHeader)), head->size);
      netSocketRecvCleanup(head);
    }
  }
}
