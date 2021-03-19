// lwip UDP and TCP echo server on port 7

#include "lwipopts.h"
#include "lwip_t41.h"
#include "lwip/inet.h"
#include "lwip/dhcp.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"

#define DO_VLANS

#define swap2 __builtin_bswap16
#define swap4 __builtin_bswap32

#ifdef DO_VLANS
struct netif vlan3_netif;
#endif

#ifdef LWIP_DEBUG
void debug_print(const char *msg) {
  Serial.print(msg);
}
#endif

static void netif_status_callback(struct netif *netif)
{
  static char str1[IP4ADDR_STRLEN_MAX], str2[IP4ADDR_STRLEN_MAX], str3[IP4ADDR_STRLEN_MAX];
  Serial.printf("netif %c%c status changed: ip %s, mask %s, gw %s\n", netif->name[0], netif->name[1], ip4addr_ntoa_r(netif_ip_addr4(netif), str1, IP4ADDR_STRLEN_MAX), ip4addr_ntoa_r(netif_ip_netmask4(netif), str2, IP4ADDR_STRLEN_MAX), ip4addr_ntoa_r(netif_ip_gw4(netif), str3, IP4ADDR_STRLEN_MAX));
}

static void link_status_callback(struct netif *netif)
{
  Serial.printf("enet netif %c%c link status: %s\n", netif->name[0], netif->name[1], netif_is_link_up(netif) ? "up" : "down");
#ifdef DO_VLANS
  if (netif == netif_default) {
    if (netif_is_link_up(netif_default)) {
      netif_set_link_up(&vlan3_netif);
    } else {
      netif_set_link_down(&vlan3_netif);
    }
  }
#endif
}

// UDP callbacks
//  fancy would be pbuf recv_q per UDP pcb, if q full, drop and free pbuf


// udp echo recv callback
void udp_callback(void * arg, struct udp_pcb * upcb, struct pbuf * p, const ip_addr_t * addr, u16_t port)
{
  if (p == NULL) return;
  udp_sendto(upcb, p, addr, port);
  pbuf_free(p);
}


void udp_echosrv() {
  struct udp_pcb *pcb;

  Serial.println("udp echosrv on port 7");
  pcb = udp_new();
  udp_bind(pcb, IP_ADDR_ANY, 7);    // local port
  udp_recv(pcb, udp_callback, NULL);  // do once?
  // fall into loop  ether_poll
}


//   ----- TCP ------

void echo_close(struct tcp_pcb * tpcb) {
  Serial.println("echo TCP  close");
  tcp_recv(tpcb, NULL);
  tcp_err(tpcb, NULL);
  tcp_close(tpcb);
}

void tcperr_callback(void * arg, err_t err)
{
  // set with tcp_err()
  Serial.print("TCP err "); Serial.println(err);
  *(int *)arg = err;
}


static  struct tcp_pcb * pcbl;   // listen pcb

void listen_err_callback(void * arg, err_t err)
{
  // set with tcp_err()
  Serial.print("TCP listen err "); Serial.println(err);
  *(int *)arg = err;
}

err_t recv_callback(void * arg, struct tcp_pcb * tpcb, struct pbuf * p, err_t err)
{
  if (p == NULL) {
    // other end closed
    echo_close(tpcb);
    return 0;
  }
  // echo it back,  not expecting our write to exceed sendqlth
  tcp_write(tpcb, p->payload, p->tot_len, TCP_WRITE_FLAG_COPY); // PUSH
  tcp_output(tpcb);    // ?needed
  tcp_recved(tpcb, p->tot_len);  // data processed
  pbuf_free(p);
  return 0;
}

err_t accept_callback(void * arg, struct tcp_pcb * newpcb, err_t err) {
  if (err || !newpcb) {
    Serial.print("accept err "); Serial.println(err);
    delay(100);
    return 1;
  }
  Serial.println("accepted");
  tcp_recv(newpcb, recv_callback);
  tcp_err(newpcb, tcperr_callback);
  //  tcp_accepted(pcbl);   // ref says the listen pcb
  return 0;
}



void tcp_echosrv() {
  struct tcp_pcb * pcb;

  pcb = tcp_new();
  tcp_bind(pcb, IP_ADDR_ANY, 7); // server port
  pcbl = tcp_listen(pcb);   // pcb deallocated
  tcp_err(pcbl, listen_err_callback);
  Serial.println("server listening on 7");
  tcp_accept(pcbl, accept_callback);

  // fall through to main ether_poll loop ....
}

err_t null_netif_init(struct netif *netif) {
  Serial.print("No netif init for ");
  Serial.print(netif->name[0]);
  Serial.println(netif->name[1]);
}

void setup()
{
#ifdef DO_VLANS
  ip4_addr addr, mask, gw;
#endif
  
  Serial.begin(115200);
  while (!Serial) delay(100);
#ifdef LWIP_DEBUG
  set_debug_print(debug_print);
#endif

  enet_init(NULL, NULL, NULL);
#ifdef DO_VLANS
  IP4_ADDR(&addr, 172, 17, 2, 115);
  IP4_ADDR(&mask, 255, 255, 255, 0);
  IP4_ADDR(&gw, 172, 17, 2, 1);
  t41_extra_netif_init(&vlan3_netif, 'e', '1');
  netif_set_vlan_id(&vlan3_netif, 3);
  netif_set_up(&vlan3_netif);
  netif_add(&vlan3_netif, &addr, &mask, &gw, 0, null_netif_init, 0);
#endif
  netif_set_status_callback(netif_default, netif_status_callback);
  netif_set_link_callback(netif_default, link_status_callback);
  netif_set_vlan_id(netif_default, 1);
  netif_set_up(netif_default);
  dhcp_start(netif_default);
#ifdef DO_VLANS
  netif_set_status_callback(&vlan3_netif, netif_status_callback);
  netif_set_link_callback(&vlan3_netif, link_status_callback);
//  dhcp_start(&vlan3_netif);
  while (!netif_is_link_up(&vlan3_netif)) loop(); // await on link up
#endif
  
  while (!netif_is_link_up(netif_default)) loop(); // await on link up
  udp_echosrv();
  tcp_echosrv();
}

void loop()
{
  static uint32_t last_ms;
  uint32_t ms;

  enet_proc_input();

  ms = millis();
  if (ms - last_ms > 100)
  {
    last_ms = ms;
    enet_poll();
  }
}
