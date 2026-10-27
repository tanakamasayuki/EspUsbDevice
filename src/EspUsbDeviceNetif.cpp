#include "EspUsbDevice.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbNetLock.h"
#include "internal/EspUsbTinyUsbRuntime.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_event.h"
#include <stdio.h>
#include <string.h>
struct EspUsbNetifGlue {
  esp_netif_driver_base_t base = {};
  EspUsbDeviceNet &owner;
  esp_netif_ip_info_t ip = {};
  char key[32] = {}, description[20] = {};
  explicit EspUsbNetifGlue(EspUsbDeviceNet &o) : owner(o) {
    snprintf(key,sizeof(key),"USB_NCM_%p",static_cast<void *>(&o));
    snprintf(description,sizeof(description),"usbncm%u",unsigned(o.interface_));
    base.post_attach = attach;
  }
  static esp_err_t attach(esp_netif_t *netif, esp_netif_iodriver_handle h) {
    static_cast<esp_netif_driver_base_t *>(h)->netif = netif; return ESP_OK;
  }
  static esp_err_t transmit(void *context, void *buffer, size_t length) {
    auto *g = static_cast<EspUsbNetifGlue *>(context);
    return g->owner.sendFrame(static_cast<const uint8_t *>(buffer),length) ? ESP_OK : ESP_FAIL;
  }
  static void freeRx(void *, void *buffer) { free(buffer); }
  static void receive(void *context, const uint8_t *data, size_t bytes) {
    auto *g = static_cast<EspUsbNetifGlue *>(context);
    void *copy = malloc(bytes); if (!copy) return;
    memcpy(copy,data,bytes);
    if (esp_netif_receive(g->base.netif,copy,bytes,copy) != ESP_OK) free(copy);
  }
  static void stop(EspUsbDeviceNet &owner) {
    auto *g = static_cast<EspUsbNetifGlue *>(owner.networkDriver_);
    if (!g) return;
    esp_netif_action_disconnected(g->base.netif,nullptr,0,nullptr);
    if (owner.dhcpClient_) esp_netif_dhcpc_stop(g->base.netif);
    if (owner.dhcpServer_) esp_netif_dhcps_stop(g->base.netif);
    esp_netif_action_stop(g->base.netif,nullptr,0,nullptr);
    esp_netif_destroy(g->base.netif);
    owner.netif_ = owner.networkDriver_ = nullptr; owner.netStarted_ = false;
    espusb::internal::deleteUsbObject(g);
  }
};
bool EspUsbDeviceNet::beginNetwork() {
  if (espusb::internal::tinyUsbRuntimeInTask() || !state_ || !device_.tinyusbStarted_ || netConfig_.maxFrameBytes < 1514 || !netConfig_.txBuffers || !netConfig_.rxBuffers) return false;
  if (netStarted_) return true;
  if (esp_netif_init() != ESP_OK) return false;
  esp_err_t error = esp_event_loop_create_default();
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return false;
  auto *g = espusb::internal::newUsbObject<EspUsbNetifGlue>(*this);
  if (!g) return false;
  const uint32_t defaultIp = uint32_t(IPAddress(192,168,7,1));
  g->ip.ip.addr = cfgIp_ ? cfgIp_ : defaultIp;
  g->ip.gw.addr = cfgGateway_ ? cfgGateway_ : g->ip.ip.addr;
  g->ip.netmask.addr = cfgNetmask_ ? cfgNetmask_ : uint32_t(IPAddress(255,255,255,0));
  esp_netif_inherent_config_t base = {};
  uint32_t flags = ESP_NETIF_FLAG_AUTOUP;
  if (dhcpServer_) flags |= ESP_NETIF_DHCP_SERVER;
  else if (dhcpClient_) flags |= ESP_NETIF_DHCP_CLIENT;
  base.flags = static_cast<esp_netif_flags_t>(flags); base.ip_info = &g->ip;
  base.if_key = g->key; base.if_desc = g->description; base.route_prio = defaultRoute_ ? 255 : 10;
  esp_netif_driver_ifconfig_t driver = {};
  driver.handle = g; driver.transmit = EspUsbNetifGlue::transmit; driver.driver_free_rx_buffer = EspUsbNetifGlue::freeRx;
  esp_netif_config_t config = {}; config.base = &base; config.driver = &driver; config.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  g->base.netif = esp_netif_new(&config);
  if (!g->base.netif || esp_netif_attach(g->base.netif,&g->base) != ESP_OK) {
    if (g->base.netif) esp_netif_destroy(g->base.netif);
    espusb::internal::deleteUsbObject(g); return false;
  }
  uint8_t mac[6]; memcpy(mac,mac_,6); mac[5] ^= 1;
  error = esp_netif_set_mac(g->base.netif,mac);
  if (error == ESP_OK) esp_netif_action_start(g->base.netif,nullptr,0,nullptr);
  if (error == ESP_OK && dhcpClient_) error = esp_netif_dhcpc_start(g->base.netif);
  else if (error == ESP_OK) {
    esp_netif_dhcps_stop(g->base.netif);
    error = esp_netif_set_ip_info(g->base.netif,&g->ip);
    if (error == ESP_OK && dhcpServer_) {
      uint8_t router = dhcpAdvertiseGateway_, dnsOffer = bool(dhcpDns_);
      if (dhcpDns_) {
        esp_netif_dns_info_t dns = {}; dns.ip.type = ESP_IPADDR_TYPE_V4; dns.ip.u_addr.ip4.addr = dhcpDns_;
        error = esp_netif_set_dns_info(g->base.netif,ESP_NETIF_DNS_MAIN,&dns);
      }
      if (error == ESP_OK) error = esp_netif_dhcps_option(g->base.netif,ESP_NETIF_OP_SET,ESP_NETIF_ROUTER_SOLICITATION_ADDRESS,&router,sizeof(router));
      if (error == ESP_OK) error = esp_netif_dhcps_option(g->base.netif,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&dnsOffer,sizeof(dnsOffer));
      if (error == ESP_OK) error = esp_netif_dhcps_start(g->base.netif);
    }
  }
  networkDriver_ = g; netif_ = g->base.netif;
  if (error != ESP_OK) { EspUsbNetifGlue::stop(*this); return false; }
  {
    espusb::internal::NetGuard guard;
    networkContext_ = g; networkReceive_ = EspUsbNetifGlue::receive;
    networkStop_ = EspUsbNetifGlue::stop; netStarted_ = true;
  }
  esp_netif_action_connected(g->base.netif,nullptr,0,nullptr);
  return true;
}
bool EspUsbDeviceNet::networkUp() const { return netStarted_ && netif_ && esp_netif_is_netif_up(static_cast<esp_netif_t *>(netif_)); }
IPAddress EspUsbDeviceNet::localIP() const {
  esp_netif_ip_info_t info;
  if (netif_ && esp_netif_get_ip_info(static_cast<esp_netif_t *>(netif_),&info) == ESP_OK) return IPAddress(info.ip.addr);
  return IPAddress(cfgIp_);
}
