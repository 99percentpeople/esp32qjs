
int main(void){
 open_manual();wifi_radio_mesh_t binding={.sta=&netif,.native=active};
 esp32_mquickjs_wifi_mesh_status_t sample={.token=active,.native_snapshot_valid=true,.parent_known=true,
  .parent_connected=true,.type_known=true,.type=MESH_STA,.events=5};
 esp_netif_ip_info_t ip;bool dhcp,ready;s_mesh->status=sample;
 /* The standalone router station requires DHCP even though it is not root. */
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready));
 assert(dhcp&&ready&&ip.ip.addr&&dhcp_starts==1);
 sample.type=MESH_NODE;s_mesh->status=sample;
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready));
 assert(!dhcp&&!ready&&!ip.ip.addr&&dhcp_state==ESP_NETIF_DHCP_STOPPED);
 sample.type=MESH_LEAF;s_mesh->status=sample;
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready)&&!dhcp&&!ready);
 sample.type=MESH_ROOT;sample.root=true;s_mesh->status=sample;
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready)&&dhcp&&ready);
 /* A control event between the IP getter and publication must retire readiness. */
 disconnect_on_ip=true;s_mesh->status=sample;
 assert(!wifi_radio_mesh_network(&binding,&sample,&ip,&dhcp,&ready));
 assert(!dhcp&&!ready&&!ip.ip.addr&&dhcp_state==ESP_NETIF_DHCP_STOPPED);
 sample=s_mesh->status;assert(!wifi_radio_mesh_ip_parent(&sample));
 sample.parent_connected=true;sample.native_snapshot_valid=true;sample.closing=true;
 assert(!wifi_radio_mesh_ip_parent(&sample));
 sample.closing=false;sample.root=false;sample.type=MESH_STA;sample.type_known=false;
 assert(!wifi_radio_mesh_ip_parent(&sample));
 s_mesh->status=(esp32_mquickjs_wifi_mesh_status_t){.token=active,.initialized=true,.started=true};
 close_manual();
}
