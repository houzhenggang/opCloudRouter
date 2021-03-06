/*
 **************************************************************************
 * Copyright (c) 2014, The Linux Foundation.  All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <linux/kthread.h>
#include <linux/sysdev.h>
#include <linux/fs.h>
#include <linux/pkt_sched.h>
#include <linux/string.h>
#include <net/ip6_route.h>
#include <net/ip6_fib.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <asm/unaligned.h>
#include <asm/uaccess.h>	/* for put_user */
#include <net/ipv6.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <linux/udp.h>
#include <linux/tcp.h>

#include <linux/inetdevice.h>
#include <linux/if_arp.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_bridge.h>
#include <linux/if_bridge.h>
#include <net/arp.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>
#include <linux/../../net/8021q/vlan.h>
#include <linux/if_vlan.h>

/*
 * Debug output levels
 * 0 = OFF
 * 1 = ASSERTS / ERRORS
 * 2 = 1 + WARN
 * 3 = 2 + INFO
 * 4 = 3 + TRACE
 */
#define DEBUG_LEVEL ECM_FRONT_END_IPV6_DEBUG_LEVEL

#include <nss_api_if.h>

#include "ecm_types.h"
#include "ecm_db_types.h"
#include "ecm_tracker.h"
#include "ecm_classifier.h"
#include "ecm_front_end_types.h"
#include "ecm_tracker_datagram.h"
#include "ecm_tracker_udp.h"
#include "ecm_tracker_tcp.h"
#include "ecm_db.h"
#include "ecm_classifier_default.h"
#include "ecm_classifier_nl.h"
#include "ecm_classifier_hyfi.h"
#include "ecm_classifier_dscp.h"
#include "ecm_interface.h"

/*
 * Magic numbers
 */
#define ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC 0xEB12
#define ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC 0xEA67
#define ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC 0xEC34

/*
 * struct ecm_front_end_ipv6_connection_tcp_instance
 *	A connection specific front end instance for TCP connections
 */
struct ecm_front_end_ipv6_connection_tcp_instance {
	struct ecm_front_end_connection_instance base;		/* Base class */
	struct ecm_db_connection_instance *ci;			/* RO: The connection instance relating to this instance. */
	bool can_accel;						/* RO: True when the connection can be accelerated */
	ecm_front_end_acceleration_mode_t accel_mode;		/* Indicates  the type of acceleration being applied to a connection, if any. */
	spinlock_t lock;					/* Lock for structure data */
	int refs;						/* Integer to trap we never go negative */
#if (DEBUG_LEVEL > 0)
	uint16_t magic;
#endif
};

/*
 * struct ecm_front_end_ipv6_connection_udp_instance
 *	A connection specific front end instance for UDP connections
 */
struct ecm_front_end_ipv6_connection_udp_instance {
	struct ecm_front_end_connection_instance base;		/* Base class */
	struct ecm_db_connection_instance *ci;			/* RO: The connection instance relating to this instance. */
	bool can_accel;						/* RO: True when the connection can be accelerated */
	ecm_front_end_acceleration_mode_t accel_mode;		/* Indicates  the type of acceleration being applied to a connection, if any. */
	spinlock_t lock;					/* Lock for structure dtaa */
	int refs;						/* Integer to trap we never go negative */
#if (DEBUG_LEVEL > 0)
	uint16_t magic;
#endif
};

/*
 * struct ecm_front_end_ipv6_connection_non_ported_instance
 *	A connection specific front end instance for Non-Ported connections
 */
struct ecm_front_end_ipv6_connection_non_ported_instance {
	struct ecm_front_end_connection_instance base;		/* Base class */
	struct ecm_db_connection_instance *ci;			/* RO: The connection instance relating to this instance. */
	bool can_accel;						/* RO: True when the connection can be accelerated */
	ecm_front_end_acceleration_mode_t accel_mode;		/* Indicates  the type of acceleration being applied to a connection, if any. */
	spinlock_t lock;					/* Lock for structure dtaa */
	int refs;						/* Integer to trap we never go negative */
#if (DEBUG_LEVEL > 0)
	uint16_t magic;
#endif
};

static int ecm_front_end_ipv6_no_action_limit_default = 250;		/* Default no-action limit. */
static int ecm_front_end_ipv6_driver_fail_limit_default = 250;		/* Default driver fail limit. */
static int ecm_front_end_ipv6_nack_limit_default = 250;			/* Default nack limit. */
static int ecm_front_end_ipv6_udp_accelerated_count = 0;		/* Number of UDP connections currently offloaded */
static int ecm_front_end_ipv6_tcp_accelerated_count = 0;		/* Number of TCP connections currently offloaded */
static int ecm_front_end_ipv6_non_ported_accelerated_count = 0;		/* Number of Non-Ported connections currently offloaded */
static int ecm_front_end_ipv6_accelerated_count = 0;			/* Total offloads */

/*
 * Locking of the classifier - concurrency control
 */
static spinlock_t ecm_front_end_ipv6_lock;			/* Protect against SMP access between netfilter, events and private threaded function. */

/*
 * SysFS linkage
 */
static struct sys_device ecm_front_end_ipv6_sys_dev;		/* SysFS linkage */

/*
 * General operational control
 */
static int ecm_front_end_ipv6_stopped = 0;			/* When non-zero further traffic will not be processed */

/*
 * Management thread control
 */
static bool ecm_front_end_ipv6_terminate_pending = false;		/* True when the user has signalled we should quit */

/*
 * NSS driver linkage
 */
static void *ecm_front_end_ipv6_nss_ipv6_context = NULL;		/* Registration for IPv6 rules */
static struct nss_ctx_instance *ecm_front_end_ipv6_nss_ipv6_mgr = NULL;

/*
 * Expose what should be a static flag in the TCP connection tracker.
 */
extern int nf_ct_tcp_no_window_check;
extern int nf_ct_tcp_be_liberal;

/*
 * ecm_front_end_ipv6_send_neighbour_solicitation()
 *	Issue an IPv6 Neighbour soliciation request.
 */
void ecm_front_end_ipv6_send_neighbour_solicitation(struct net_device *dev, ip_addr_t addr)
{
	struct in6_addr dst_addr, src_addr;
	struct in6_addr mc_dst_addr;
	struct rt6_info *rt6i;
	struct neighbour *neigh;
	ip_addr_t ecm_mc_dst_addr, ecm_src_addr;
	struct net *netf = dev_net(dev);
	int ret;

	char __attribute__((unused)) dst_addr_str[40];
	char __attribute__((unused)) mc_dst_addr_str[40];
	char __attribute__((unused)) src_addr_str[40];

	/*
	 * Find source and destination addresses in Linux format. We need
	 * mcast destination address as well.
	 */
	ECM_IP_ADDR_TO_NIN6_ADDR(dst_addr, addr);
	addrconf_addr_solict_mult(&dst_addr, &mc_dst_addr);
	ret = ipv6_dev_get_saddr(netf, dev, &dst_addr, 0, &src_addr);

	/*
	 * IP address in string format for debug
	 */
	ecm_ip_addr_to_string(dst_addr_str, addr);
	ECM_NIN6_ADDR_TO_IP_ADDR(ecm_mc_dst_addr, mc_dst_addr);
	ecm_ip_addr_to_string(mc_dst_addr_str, ecm_mc_dst_addr);
	ECM_NIN6_ADDR_TO_IP_ADDR(ecm_src_addr, src_addr);
	ecm_ip_addr_to_string(src_addr_str, ecm_src_addr);

	/*
	 * Find the route entry
	 */
	rt6i = rt6_lookup(netf, &dst_addr, NULL, 0, 0);
	if (!rt6i) {
		DEBUG_TRACE("IPv6 Route lookup failure for destination IPv6 address %s\n", dst_addr_str);
		return;
		// GGG TODO Flatten
	} else {
		/*
		 * Find the neighbor entry
		 */
		neigh = rt6i->dst.ops->neigh_lookup(&rt6i->dst, &dst_addr);
		if (neigh == NULL) {
			DEBUG_TRACE("Neighbour lookup failure for destination IPv6 address %s\n", dst_addr_str);
			return;
		} else {
			/*
			 * Issue a Neighbour soliciation request
			 */
			DEBUG_TRACE("Issue Neighbour solicitation request\n");
			ndisc_send_ns(dev, neigh, &dst_addr, &mc_dst_addr, &src_addr);
			neigh_release(neigh);
		}
	}
}

/*
 * ecm_front_end_ipv6_interface_heirarchy_construct()
 *	Construct an interface heirarchy.
 *
 * Using the given addressing, locate the interface heirarchy used to emit packets to that destination.
 * This is the heirarchy of interfaces a packet would transit to emit from the device.
 *
 * We will use the given src/dest devices when is_routed is false.
 * When is_routed is true we will try routing tables first, failing back to any given.
 *
 * For example, with this network arrangement:
 *
 * PPPoE--VLAN--BRIDGE--BRIDGE_PORT(LAG_MASTER)--LAG_SLAVE_0--10.22.33.11
 *
 * Given the IP address 10.22.33.11 this will create an interface heirarchy (in interracfes[]) of:
 * LAG_SLAVE_0 @ [ECM_DB_IFACE_HEIRARCHY_MAX - 5]
 * LAG_MASTER @ [ECM_DB_IFACE_HEIRARCHY_MAX - 4]
 * BRIDGE @ [ECM_DB_IFACE_HEIRARCHY_MAX - 3]
 * VLAN @ [ECM_DB_IFACE_HEIRARCHY_MAX - 2]
 * PPPOE @ [ECM_DB_IFACE_HEIRARCHY_MAX - 1]
 * The value returned is (ECM_DB_IFACE_HEIRARCHY_MAX - 5)
 *
 * IMPORTANT: This function will return any known interfaces in the database, when interfaces do not exist in the database
 * they will be created and added automatically to the database.
 *
 * GGG TODO Remove this in favour of the ecm_interface version - understand any refactoring needs first.
 */
int32_t ecm_front_end_ipv6_interface_heirarchy_construct(struct ecm_db_iface_instance *interfaces[], ip_addr_t packet_src_addr, ip_addr_t packet_dest_addr, int packet_protocol,
								struct net_device *given_dest_dev, bool is_routed, struct net_device *given_src_dev, uint8_t *dest_node_addr, uint8_t *src_node_addr)
{
	int protocol;
	ip_addr_t src_addr;
	ip_addr_t dest_addr;
	struct net_device *dest_dev;
	char *dest_dev_name;
	int32_t dest_dev_type;
	struct net_device *src_dev;
	char *src_dev_name;
	int32_t src_dev_type;
	int32_t current_interface_index;
	bool from_local_addr;

	/*
	 * Get a big endian of the IPv6 address we have been given as our starting point.
	 */
	protocol = packet_protocol;
	ECM_IP_ADDR_COPY(src_addr, packet_src_addr);
	ECM_IP_ADDR_COPY(dest_addr, packet_dest_addr);
	DEBUG_TRACE("Construct interface heirarchy for from src_addr: " ECM_IP_ADDR_OCTAL_FMT " to dest_addr: " ECM_IP_ADDR_OCTAL_FMT ", protocol: %d\n",
			ECM_IP_ADDR_TO_OCTAL(src_addr), ECM_IP_ADDR_TO_OCTAL(dest_addr), protocol);

	/*
	 * Get device to reach the given destination address.
	 * If the heirarchy is for a routed connection we must try route lookup first, falling back to any given_dest_dev.
	 * If the heirarchy is NOT for a routed connection we try the given_dest_dev first, followed by routed lookup.
	 */
	from_local_addr = false;
	if (is_routed) {
		dest_dev = ecm_interface_dev_find_by_addr(dest_addr, &from_local_addr);
		if (!dest_dev && given_dest_dev) {
			/*
			 * Fall back to any given
			 */
			dest_dev = given_dest_dev;
			dev_hold(dest_dev);
		}
	} else if (given_dest_dev) {
		dest_dev = given_dest_dev;
		dev_hold(dest_dev);
	} else {
		/*
		 * Fall back to routed look up
		 */
		dest_dev = ecm_interface_dev_find_by_addr(dest_addr, &from_local_addr);
	}

	/*
	 * GGG ALERT: If the address is a local address and protocol is an IP tunnel
	 * then this connection is a tunnel endpoint made to this device.
	 * In which case we circumvent all proper procedure and just hack the devices to make stuff work.
	 * GGG TODO THIS MUST BE FIXED - WE MUST USE THE INTERFACE HIERARCHY FOR ITS INTENDED PURPOSE TO
	 * PARSE THE DEVICES AND WORK OUT THE PROPER INTERFACES INVOLVED.
	 * E.G. IF WE TRIED TO RUN A TUNNEL OVER A VLAN OR QINQ THIS WILL BREAK AS WE DON'T DISCOVER THAT HIERARCHY
	 */
	if (dest_dev && from_local_addr && (protocol == IPPROTO_IPIP)) {
		dev_put(dest_dev);
		dest_dev = given_dest_dev;
		if (dest_dev) {
			dev_hold(dest_dev);
			DEBUG_TRACE("HACK: IPIP tunnel packet with dest_addr: " ECM_IP_ADDR_OCTAL_FMT " uses dev: %p(%s)\n", ECM_IP_ADDR_TO_OCTAL(dest_addr), dest_dev, dest_dev->name);
		}
	}
	if (!dest_dev) {
		DEBUG_WARN("dest_addr: " ECM_IP_ADDR_OCTAL_FMT " - cannot locate device\n", ECM_IP_ADDR_TO_OCTAL(dest_addr));
		return ECM_DB_IFACE_HEIRARCHY_MAX;
	}
	dest_dev_name = dest_dev->name;
	dest_dev_type = dest_dev->type;

	/*
	 * Get device to reach the given source address.
	 * If the heirarchy is for a routed connection we must try route lookup first, falling back to any given_src_dev.
	 * If the heirarchy is NOT for a routed connection we try the given_src_dev first, followed by routed lookup.
	 */
	from_local_addr = false;
	if (is_routed) {
		src_dev = ecm_interface_dev_find_by_addr(src_addr, &from_local_addr);
		if (!src_dev && given_src_dev) {
			/*
			 * Fall back to any given
			 */
			src_dev = given_src_dev;
			dev_hold(src_dev);
		}
	} else if (given_src_dev) {
		src_dev = given_src_dev;
		dev_hold(src_dev);
	} else {
		/*
		 * Fall back to routed look up
		 */
		src_dev = ecm_interface_dev_find_by_addr(src_addr, &from_local_addr);
	}

	/*
	 * GGG ALERT: If the address is a local address and protocol is an IP tunnel
	 * then this connection is a tunnel endpoint made to this device.
	 * In which case we circumvent all proper procedure and just hack the devices to make stuff work.
	 * GGG TODO THIS MUST BE FIXED - WE MUST USE THE INTERFACE HIERARCHY FOR ITS INTENDED PURPOSE TO
	 * PARSE THE DEVICES AND WORK OUT THE PROPER INTERFACES INVOLVED.
	 * E.G. IF WE TRIED TO RUN A TUNNEL OVER A VLAN OR QINQ THIS WILL BREAK AS WE DON'T DISCOVER THAT HIERARCHY
	 */
	if (src_dev && from_local_addr && (protocol == IPPROTO_IPIP)) {
		dev_put(src_dev);
		src_dev = given_src_dev;
		if (src_dev) {
			dev_hold(src_dev);
			DEBUG_TRACE("HACK: IPIP tunnel packet with src_addr: " ECM_IP_ADDR_OCTAL_FMT " uses dev: %p(%s)\n", ECM_IP_ADDR_TO_OCTAL(src_addr), src_dev, src_dev->name);
		}
	}
	if (!src_dev) {
		DEBUG_WARN("src_addr: " ECM_IP_ADDR_OCTAL_FMT " - cannot locate device\n", ECM_IP_ADDR_TO_OCTAL(src_addr));
		dev_put(dest_dev);
		return ECM_DB_IFACE_HEIRARCHY_MAX;
	}
	src_dev_name = src_dev->name;
	src_dev_type = src_dev->type;

	/*
	 * Check if source and dest dev are same.
	 * For the forwarded flows which involve tunnels this will happen when called from input hook.
	 */
	if (src_dev == dest_dev) {
		DEBUG_TRACE("Protocol is :%d source dev and dest dev are same\n", protocol);
		if (protocol == IPPROTO_IPIP) {
			/*
			 * This happens from the input hook
			 * We do not want to create a connection entry for this
			 * GGG TODO YES WE DO.
			 * GGG TODO THIS CONCERNS ME AS THIS SHOULD BE CAUGHT MUCH
			 * EARLIER IN THE FRONT END IF POSSIBLE TO AVOID PERFORMANCE PENALTIES.
			 * WE HAVE DONE A TREMENDOUS AMOUT OF WORK TO GET TO THIS POINT.
			 * WE WILL ABORT HERE AND THIS WILL BE REPEATED FOR EVERY PACKET.
			 * IN KEEPING WITH THE ECM DESIGN IT IS BETTER TO CREATE A CONNECTION AND RECORD IN THE HIERARCHY
			 * ENOUGH INFORMATION TO ENSURE THAT ACCELERATION IS NOT BROKEN / DOES NOT OCCUR AT ALL.
			 * THAT WAY WE DO A HEAVYWEIGHT ESTABLISHING OF A CONNECTION ONCE AND NEVER AGAIN...
			 */
			dev_put(src_dev);
			dev_put(dest_dev);
			return ECM_DB_IFACE_HEIRARCHY_MAX;
		}
	}

	/*
	 * Iterate until we are done or get to the max number of interfaces we can record.
	 * NOTE: current_interface_index tracks the position of the first interface position in interfaces[]
	 * because we add from the end first_interface grows downwards.
	 */
	current_interface_index = ECM_DB_IFACE_HEIRARCHY_MAX;
	while (current_interface_index > 0) {
		struct ecm_db_iface_instance *ii;
		struct net_device *next_dev;

		/*
		 * Get the ecm db interface instance for the device at hand
		 */
		ii = ecm_interface_establish_and_ref(dest_dev);

		/*
		 * If the interface could not be established then we abort
		 */
		if (!ii) {
			DEBUG_WARN("Failed to establish interface: %p, name: %s\n", dest_dev, dest_dev_name);
			dev_put(src_dev);
			dev_put(dest_dev);

			/*
			 * Release the interfaces heirarchy we constructed to this point.
			 */
			ecm_db_connection_interfaces_deref(interfaces, current_interface_index);
			return ECM_DB_IFACE_HEIRARCHY_MAX;
		}

		/*
		 * Record the interface instance into the interfaces[]
		 */
		current_interface_index--;
		interfaces[current_interface_index] = ii;

		/*
		 * Now we have to figure out what the next device will be (in the transmission path) the skb
		 * will use to emit to the destination address.
		 */
		do {
#ifdef ECM_INTERFACE_PPP_SUPPORT
			int channel_count;
			struct ppp_channel *ppp_chan[1];
			int channel_protocol;
			struct pppoe_opt addressing;
#endif

			DEBUG_TRACE("Net device: %p is type: %d, name: %s\n", dest_dev, dest_dev_type, dest_dev_name);
			next_dev = NULL;

			if (dest_dev_type == ARPHRD_ETHER) {
				/*
				 * Ethernet - but what sub type?
				 */

				/*
				 * VLAN?
				 */
				if (is_vlan_dev(dest_dev)) {
					/*
					 * VLAN master
					 * No locking needed here, ASSUMPTION is that real_dev is held for as long as we have dev.
					 */
					next_dev = vlan_dev_priv(dest_dev)->real_dev;
					dev_hold(next_dev);
					DEBUG_TRACE("Net device: %p is VLAN, slave dev: %p (%s)\n",
							dest_dev, next_dev, next_dev->name);
					break;
				}

				/*
				 * BRIDGE?
				 */
				if (ecm_front_end_is_bridge_device(dest_dev)) {
					/*
					 * Bridge
					 * Figure out which port device the skb will go to using the dest_addr.
					 */
					bool on_link;
					ip_addr_t gw_addr;
					uint8_t mac_addr[ETH_ALEN];
					if (!ecm_interface_mac_addr_get(dest_addr, mac_addr, &on_link, gw_addr)) {
						/*
						 * Possible ARP does not know the address yet
						 */
						DEBUG_WARN("Unable to obtain MAC address for " ECM_IP_ADDR_OCTAL_FMT "\n", ECM_IP_ADDR_TO_OCTAL(dest_addr));
						ecm_front_end_ipv6_send_neighbour_solicitation(dest_dev, dest_addr);
						dev_put(src_dev);
						dev_put(dest_dev);

						/*
						 * Release the interfaces heirarchy we constructed to this point.
						 */
						ecm_db_connection_interfaces_deref(interfaces, current_interface_index);
						return ECM_DB_IFACE_HEIRARCHY_MAX;
					}
					next_dev = br_port_dev_get(dest_dev, mac_addr);
					if (!next_dev) {
						DEBUG_WARN("Unable to obtain output port for: %pM\n", mac_addr);
						dev_put(src_dev);
						dev_put(dest_dev);

						/*
						 * Release the interfaces heirarchy we constructed to this point.
						 */
						ecm_db_connection_interfaces_deref(interfaces, current_interface_index);
						return ECM_DB_IFACE_HEIRARCHY_MAX;
					}
					DEBUG_TRACE("Net device: %p is BRIDGE, next_dev: %p (%s)\n", dest_dev, next_dev, next_dev->name);
					break;
				}

				/*
				 * LAG?
				 */
				if (ecm_front_end_is_lag_master(dest_dev)) {
					/*
					 * Link aggregation
					 * Figure out which slave device of the link aggregation will be used to reach the destination.
					 */
					bool dest_on_link = false;
					ip_addr_t dest_gw_addr = ECM_IP_ADDR_NULL;
					uint8_t src_mac_addr[ETH_ALEN];
					uint8_t dest_mac_addr[ETH_ALEN];
					struct net_device *master_dev = NULL;

					memset(src_mac_addr, 0, ETH_ALEN);
					memset(dest_mac_addr, 0, ETH_ALEN);

					if (!is_routed) {
						memcpy(src_mac_addr, src_node_addr, ETH_ALEN);
						memcpy(dest_mac_addr, dest_node_addr, ETH_ALEN);
					} else {
						if (dest_dev->master) {
							memcpy(src_mac_addr, dest_dev->master->dev_addr, ETH_ALEN);
						} else {
							memcpy(src_mac_addr, dest_dev->dev_addr, ETH_ALEN);
						}

						if (!ecm_interface_mac_addr_get(dest_addr, dest_mac_addr, &dest_on_link, dest_gw_addr)) {
							/*
							 * Possible ARP does not know the address yet
							 */
							DEBUG_WARN("Unable to obtain MAC address for " ECM_IP_ADDR_DOT_FMT "\n", ECM_IP_ADDR_TO_DOT(dest_addr));

							/*
							 * find proper interfce from which to issue neighbour solicitation
							 */
							if (dest_dev->master) {
								master_dev = dest_dev->master;
							} else {
								master_dev = dest_dev;
							}

							dev_hold(master_dev);

							ecm_front_end_ipv6_send_neighbour_solicitation(master_dev, dest_addr);
							dev_put(src_dev);
							dev_put(dest_dev);
							dev_put(master_dev);

							/*
							 * Release the interfaces heirarchy we constructed to this point.
							 */
							ecm_db_connection_interfaces_deref(interfaces, current_interface_index);
							return ECM_DB_IFACE_HEIRARCHY_MAX;
						}
					}

					next_dev = bond_get_tx_dev(NULL, src_mac_addr, dest_mac_addr, src_addr, dest_addr, htons((uint16_t)ETH_P_IPV6), dest_dev);
					if (next_dev && netif_carrier_ok(next_dev)) {
						dev_hold(next_dev);
					} else {
						DEBUG_WARN("Unable to obtain LAG output slave device\n");
						dev_put(src_dev);
						dev_put(dest_dev);

						/*
						 * Release the interfaces heirarchy we constructed to this point.
						 */
						ecm_db_connection_interfaces_deref(interfaces, current_interface_index);
						return ECM_DB_IFACE_HEIRARCHY_MAX;
					}

					DEBUG_TRACE("Net device: %p is LAG, slave dev: %p (%s)\n", dest_dev, next_dev, next_dev->name);

					break;
				}

				/*
				 * ETHERNET!
				 * Just plain ethernet it seems.
				 */
				DEBUG_TRACE("Net device: %p is ETHERNET\n", dest_dev);
				break;
			}

			/*
			 * LOOPBACK?
			 */
			if (dest_dev_type == ARPHRD_LOOPBACK) {
				DEBUG_TRACE("Net device: %p is LOOPBACK type: %d\n", dest_dev, dest_dev_type);
				break;
			}

			/*
			 * IPSEC?
			 */
			if (dest_dev_type == ECM_ARPHRD_IPSEC_TUNNEL_TYPE) {
				DEBUG_TRACE("Net device: %p is IPSec tunnel type: %d\n", dest_dev, dest_dev_type);
				// GGG TODO Figure out the next device the tunnel is using...
				break;
			}

			/*
			 * SIT (6-in-4)?
			 */
			if (dest_dev_type == ARPHRD_SIT) {
				DEBUG_TRACE("Net device: %p is SIT (6-in-4) type: %d\n", dest_dev, dest_dev_type);
				// GGG TODO Figure out the next device the tunnel is using...
				break;
			}

			/*
			 * IPIP6 Tunnel?
			 */
			if (dest_dev_type == ARPHRD_TUNNEL6) {
				DEBUG_TRACE("Net device: %p is TUNIPIP6 type: %d\n", dest_dev, dest_dev_type);
				// GGG TODO Figure out the next device the tunnel is using...
				break;
			}

			/*
			 * If this is NOT PPP then it is unknown to the ecm and we cannot figure out it's next device.
			 */
			if (dest_dev_type != ARPHRD_PPP) {
				DEBUG_TRACE("Net device: %p is UNKNOWN type: %d\n", dest_dev, dest_dev_type);
				break;
			}

#ifndef ECM_INTERFACE_PPP_SUPPORT
			DEBUG_TRACE("Net device: %p is UNKNOWN (PPP Unsupported) type: %d\n", dest_dev, dest_dev_type);
#else
			/*
			 * PPP - but what is the channel type?
			 * First: If this is multi-link then we do not support it
			 */
			if (ppp_is_multilink(dest_dev) > 0) {
				DEBUG_TRACE("Net device: %p is MULTILINK PPP - Unknown to the ECM\n", dest_dev);
				break;
			}

			DEBUG_TRACE("Net device: %p is PPP\n", dest_dev);

			/*
			 * Get the PPP channel and then enquire what kind of channel it is
			 * NOTE: Not multilink so only one channel to get.
			 */
			channel_count = ppp_hold_channels(dest_dev, ppp_chan, 1);
			if (channel_count != 1) {
				DEBUG_TRACE("Net device: %p PPP has %d channels - ECM cannot handle this (interface becomes Unknown type)\n",
						dest_dev, channel_count);
				break;
			}

			/*
			 * Get channel protocol type
			 * NOTE: Not all PPP channels support channel specific methods.
			 */
			channel_protocol = ppp_channel_get_protocol(ppp_chan[0]);
			if (channel_protocol != PX_PROTO_OE) {
				DEBUG_TRACE("Net device: %p PPP channel protocol: %d - Unknown to the ECM\n",
						dest_dev, channel_protocol);

				/*
				 * Release the channel
				 */
				ppp_release_channels(ppp_chan, 1);

				break;
			}

			/*
			 * PPPoE channel
			 */
			DEBUG_TRACE("Net device: %p PPP channel is PPPoE\n", dest_dev);

			/*
			 * Get PPPoE session information and the underlying device it is using.
			 */
			pppoe_channel_addressing_get(ppp_chan[0], &addressing);

			/*
			 * Copy the dev hold into this, we will release the hold later
			 */
			next_dev = addressing.dev;

			DEBUG_TRACE("Net device: %p, next device: %p (%s)\n", dest_dev, next_dev, next_dev->name);

			/*
			 * Release the channel.  Note that next_dev is still (correctly) held.
			 */
			ppp_release_channels(ppp_chan, 1);
#endif
		} while (false);

		/*
		 * No longer need dest_dev as it may become next_dev
		 */
		dev_put(dest_dev);

		/*
		 * Check out the next_dev, if any
		 */
		if (!next_dev) {
			int32_t i __attribute__((unused));
			DEBUG_INFO("Completed interface heirarchy construct with first interface @: %d\n", current_interface_index);
#if DEBUG_LEVEL > 1
			for (i = current_interface_index; i < ECM_DB_IFACE_HEIRARCHY_MAX; ++i) {
				DEBUG_TRACE("\tInterface @ %d: %p, type: %d, name: %s\n",
						i, interfaces[i], ecm_db_connection_iface_type_get(interfaces[i]),
						ecm_db_interface_type_to_string(ecm_db_connection_iface_type_get(interfaces[i])));
			}
#endif
			/*
			 * Release src_dev now
			 */
			dev_put(src_dev);
			return current_interface_index;
		}

		/*
		 * dest_dev becomes next_dev
		 */
		dest_dev = next_dev;
		dest_dev_name = dest_dev->name;
		dest_dev_type = dest_dev->type;
	}

	DEBUG_WARN("Too many interfaces: %d\n", current_interface_index);
	DEBUG_ASSERT(current_interface_index == 0, "Bad logic handling current_interface_index: %d\n", current_interface_index);
	dev_put(src_dev);
	dev_put(dest_dev);

	/*
	 * Release the interfaces heirarchy we constructed to this point.
	 */
	ecm_db_connection_interfaces_deref(interfaces, current_interface_index);
	return ECM_DB_IFACE_HEIRARCHY_MAX;
}

/*
 * ecm_front_end_ipv6_node_establish_and_ref()
 *	Returns a reference to a node, possibly creating one if necessary.
 *
 * The given_node_addr will be used if provided.
 *
 * Returns NULL on failure.
 */
static struct ecm_db_node_instance *ecm_front_end_ipv6_node_establish_and_ref(struct net_device *dev, ip_addr_t addr,
							struct ecm_db_iface_instance *interface_list[], int32_t interface_list_first,
							uint8_t *given_node_addr)
{
	struct ecm_db_node_instance *ni;
	struct ecm_db_node_instance *nni;
	struct ecm_db_iface_instance *ii;
	int i;
	bool done;
	uint8_t node_addr[ETH_ALEN];

	DEBUG_INFO("Establish node for " ECM_IP_ADDR_OCTAL_FMT "\n", ECM_IP_ADDR_TO_OCTAL(addr));

	/*
	 * The node is the datalink address, typically a MAC address.
	 * However the node address to use is not always obvious and depends on the interfaces involved.
	 * For example if the interface is PPPoE then we use the MAC of the PPPoE server as we cannot use normal ARP resolution.
	 * Not all hosts have a node address, where there is none, a suitable alternative should be located and is typically based on 'addr'
	 * or some other datalink session information.
	 * It should be, at a minimum, something that ties the host with the interface.
	 *
	 * Iterate from 'inner' to 'outer' interfaces - discover what the node is.
	 */
	memset(node_addr, 0, ETH_ALEN);
	done = false;
	if (given_node_addr) {
		memcpy(node_addr, given_node_addr, ETH_ALEN);
		done = true;
		DEBUG_TRACE("Using given node address: %pM\n", node_addr);
	}
	for (i = ECM_DB_IFACE_HEIRARCHY_MAX - 1; (!done) && (i >= interface_list_first); i--) {
		ecm_db_iface_type_t type;
		ip_addr_t gw_addr = ECM_IP_ADDR_NULL;
		bool on_link = false;
		struct ecm_db_interface_info_pppoe pppoe_info;

		type = ecm_db_connection_iface_type_get(interface_list[i]);
		DEBUG_INFO("Lookup node address, interface @ %d is type: %d\n", i, type);

		switch (type) {

		case ECM_DB_IFACE_TYPE_PPPOE:
			/*
			 * Node address is the address of the remote PPPoE server
			 */
			ecm_db_iface_pppoe_session_info_get(interface_list[i], &pppoe_info);
			memcpy(node_addr, pppoe_info.remote_mac, ETH_ALEN);
			done = true;
			break;

		case ECM_DB_IFACE_TYPE_SIT:
		case ECM_DB_IFACE_TYPE_TUNIPIP6:
			done = true;
			break;

		case ECM_DB_IFACE_TYPE_ETHERNET:
		case ECM_DB_IFACE_TYPE_LAG:
		case ECM_DB_IFACE_TYPE_VLAN:
		case ECM_DB_IFACE_TYPE_BRIDGE:
			if (!ecm_interface_mac_addr_get(addr, node_addr, &on_link, gw_addr)) {
				DEBUG_TRACE("Failed to obtain mac for host " ECM_IP_ADDR_OCTAL_FMT "\n", ECM_IP_ADDR_TO_OCTAL(addr));
				if (ecm_front_end_is_bridge_port(dev)) {
					ecm_front_end_ipv6_send_neighbour_solicitation(dev->master, addr);
				} else {
					ecm_front_end_ipv6_send_neighbour_solicitation(dev, addr);
				}
				return NULL;
			}
			if (is_multicast_ether_addr(node_addr)) {
				DEBUG_TRACE("multicast node address for host " ECM_IP_ADDR_OCTAL_FMT ", node_addr: %pM\n", ECM_IP_ADDR_TO_OCTAL(addr), node_addr);
				return NULL;
			}

			done = true;
			break;
		default:
			/*
			 * Don't know how to handle these.
			 * Just copy some part of the address for now, but keep iterating the interface list
			 * in the hope something recognisable will be seen!
			 * GGG TODO We really need to roll out support for all interface types we can deal with ASAP :-(
			 */
			memcpy(node_addr, (uint8_t *)addr, ETH_ALEN);
		}
	}
	if (!done) {
		DEBUG_INFO("Failed to establish node for " ECM_IP_ADDR_OCTAL_FMT "\n", ECM_IP_ADDR_TO_OCTAL(addr));
		return NULL;
	}

	/*
	 * Locate the node
	 */
	ni = ecm_db_node_find_and_ref(node_addr);
	if (ni) {
		DEBUG_TRACE("%p: node established\n", ni);
		return ni;
	}

	/*
	 * No node - establish iface
	 */
	ii = ecm_interface_establish_and_ref(dev);
	if (!ii) {
		DEBUG_WARN("Failed to establish iface\n");
		return NULL;
	}

	/*
	 * No node - create one
	 */
	nni = ecm_db_node_alloc();
	if (!nni) {
		DEBUG_WARN("Failed to establish node\n");
		ecm_db_iface_deref(ii);
		return NULL;
	}

	/*
	 * Add node into the database, atomically to avoid races creating the same thing
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ni = ecm_db_node_find_and_ref(node_addr);
	if (ni) {
		spin_unlock_bh(&ecm_front_end_ipv6_lock);
		ecm_db_node_deref(nni);
		ecm_db_iface_deref(ii);
		return ni;
	}

	ecm_db_node_add(nni, ii, node_addr, NULL, nni);
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Don't need iface instance now
	 */
	ecm_db_iface_deref(ii);

	DEBUG_TRACE("%p: node established\n", nni);
	return nni;
}

/*
 * ecm_front_end_ipv6_host_establish_and_ref()
 *	Returns a reference to a host, possibly creating one if necessary.
 *
 * Returns NULL on failure.
 */
static struct ecm_db_host_instance *ecm_front_end_ipv6_host_establish_and_ref(struct net_device *dev, ip_addr_t addr,
						struct ecm_db_iface_instance *interface_list[], int32_t interface_list_first,
						uint8_t *given_node_addr)
{
	struct ecm_db_host_instance *hi;
	struct ecm_db_host_instance *nhi;
	struct ecm_db_node_instance *ni;

	DEBUG_INFO("Establish host for " ECM_IP_ADDR_OCTAL_FMT "\n", ECM_IP_ADDR_TO_OCTAL(addr));

	/*
	 * Locate the host
	 */
	hi = ecm_db_host_find_and_ref(addr);
	if (hi) {
		DEBUG_TRACE("%p: host established\n", hi);
		return hi;
	}

	ni = ecm_front_end_ipv6_node_establish_and_ref(dev, addr, interface_list, interface_list_first, given_node_addr);
	if (!ni) {
		DEBUG_WARN("Failed to establish node\n");
		return NULL;
	}

	/*
	 * No host - create one
	 */
	nhi = ecm_db_host_alloc();
	if (!nhi) {
		DEBUG_WARN("Failed to establish host\n");
		ecm_db_node_deref(ni);
		return NULL;
	}

	/*
	 * Add host into the database, atomically to avoid races creating the same thing
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	hi = ecm_db_host_find_and_ref(addr);
	if (hi) {
		spin_unlock_bh(&ecm_front_end_ipv6_lock);
		ecm_db_host_deref(nhi);
		ecm_db_node_deref(ni);
		return hi;
	}

	ecm_db_host_add(nhi, ni, addr, true, NULL, nhi);

	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Don't need node instance now
	 */
	ecm_db_node_deref(ni);

	DEBUG_TRACE("%p: host established\n", nhi);
	return nhi;
}

/*
 * _ecm_front_end_ipv6_mapping_establish_and_ref()
 *	Returns a reference to a mapping, possibly creating one if necessary.
 *
 * Returns NULL on failure.
 */
static struct ecm_db_mapping_instance *ecm_front_end_ipv6_mapping_establish_and_ref(struct net_device *dev, ip_addr_t addr, int port,
						struct ecm_db_iface_instance *interface_list[], int32_t interface_list_first,
						uint8_t *given_node_addr)
{
	struct ecm_db_mapping_instance *mi;
	struct ecm_db_mapping_instance *nmi;
	struct ecm_db_host_instance *hi;

	DEBUG_INFO("%p: Establish mapping for " ECM_IP_ADDR_OCTAL_FMT ":%u\n", dev, ECM_IP_ADDR_TO_OCTAL(addr), port);

	/*
	 * No mapping - establish host existence
	 */
	hi = ecm_front_end_ipv6_host_establish_and_ref(dev, addr, interface_list, interface_list_first, given_node_addr);
	if (!hi) {
		DEBUG_WARN("Failed to establish host\n");
		return NULL;
	}

	/*
	 * Locate the mapping
	 */
	mi = ecm_db_mapping_find_and_ref(addr, port);
	if (mi) {
		DEBUG_TRACE("%p: mapping established\n", mi);
		return mi;
	}

	/*
	 * Create mapping instance
	 */
	nmi = ecm_db_mapping_alloc();
	if (!nmi) {
		ecm_db_host_deref(hi);
		DEBUG_WARN("Failed to establish mapping\n");
		return NULL;
	}

	/*
	 * Add mapping into the database, atomically to avoid races creating the same thing
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	mi = ecm_db_mapping_find_and_ref(addr, port);
	if (mi) {
		spin_unlock_bh(&ecm_front_end_ipv6_lock);
		ecm_db_mapping_deref(nmi);
		ecm_db_host_deref(hi);
		return mi;
	}

	ecm_db_mapping_add(nmi, hi, port, NULL, nmi);

	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Don't need the host instance now - the mapping maintains a reference to it now.
	 */
	ecm_db_host_deref(hi);

	/*
	 * Return the mapping instance
	 */
	DEBUG_INFO("%p: mapping established\n", nmi);
	return nmi;
}

/*
 * ecm_front_end_ipv6_connection_tcp_callback()
 *	Callback for handling create ack/nack calls.
 *
 * TODO: Since we don't want to hold data structure reference across multiple heterogenous
 * CPUs, we should use the app_data as the sequence # and do a fast look up rather than
 * a 5-tuple lookup.
 */
static void ecm_front_end_ipv6_connection_tcp_callback(void *app_data, struct nss_ipv6_msg *nim)
{
	struct nss_ipv6_rule_create_msg *nircm = &nim->msg.rule_create;
	uint32_t serial = (uint32_t)app_data;
	struct ecm_db_connection_instance *ci;
	struct ecm_front_end_connection_instance *feci;
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti;
	ip_addr_t flow_ip;
	ip_addr_t return_ip;

	/*
	 * Is this a response to a create message?
	 */
	if (nim->cm.type != NSS_IPV6_TX_CREATE_RULE_MSG) {
		DEBUG_ERROR("%p: tcp create callback with improper type: %d, serial: %u\n", nim, nim->cm.type, serial);
		return;
	}

	/*
	 * Look up ecm connection so that we can update the status.
	 */
	ci = ecm_db_connection_serial_find_and_ref(serial);
	if (!ci) {
		DEBUG_TRACE("%p: create callback, connection not found, serial: %u\n", nim, serial);
		return;
	}

	/*
	 * Get the front end instance
	 */
	feci = ecm_db_connection_front_end_get_and_ref(ci);
	fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;
	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);

	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(flow_ip, nircm->tuple.flow_ip);
	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(return_ip, nircm->tuple.return_ip);

	/*
	 * Dump some useful trace information.
	 */
	DEBUG_TRACE("%p: accelerate response for connection: %p, serial: %u\n", fecti, fecti->ci, serial);
	DEBUG_TRACE("%p: rule_flags: %x, valid_flags: %x\n", fecti, nircm->rule_flags, nircm->valid_flags);
	DEBUG_TRACE("%p: flow_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecti, ECM_IP_ADDR_TO_OCTAL(flow_ip), nircm->tuple.flow_ident);
	DEBUG_TRACE("%p: return_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecti, ECM_IP_ADDR_TO_OCTAL(return_ip), nircm->tuple.return_ident);
	DEBUG_TRACE("%p: protocol: %d\n", fecti, nircm->tuple.protocol);

	/*
	 * Handle the creation result code.
	 */
	DEBUG_TRACE("%p: response: %d\n", fecti, nim->cm.response);
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		/*
		 * Creation command failed (specific reason ignored).
		 */
		DEBUG_TRACE("%p: accel nack: %d\n", fecti, nim->cm.error);
		spin_lock_bh(&fecti->lock);
		DEBUG_ASSERT(fecti->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Unexpected mode: %d\n", ci, fecti->accel_mode);
		fecti->base.stats.nss_nack++;
		fecti->base.stats.nss_nack_total++;
		if (fecti->base.stats.nss_nack >= fecti->base.stats.nss_nack_limit) {
			/*
			 * Too many NSS rejections
			 */
			fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_NSS;
		} else {
			/*
			 * Revert to decelerated
			 */
			fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
		}

		/*
		 * Clear any decelerate pending flag since we aren't accelerated anyway we can just clear this whether it is set or not
		 */
		fecti->base.stats.decelerate_pending = false;
		spin_unlock_bh(&fecti->lock);

		/*
		 * Release the connection.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	/*
	 * Create succeeded, declare that we are accelerated.
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_tcp_accelerated_count++;	/* Protocol specific counter */
	ecm_front_end_ipv6_accelerated_count++;		/* General running counter */
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	spin_lock_bh(&fecti->lock);
	DEBUG_ASSERT(fecti->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Unexpected mode: %d\n", ci, fecti->accel_mode);
	fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_ACCEL;

	/*
	 * Increement the no-action counter, this is reset if offload action is seen
	 */
	fecti->base.stats.no_action_seen++;

	/*
	 * Clear any nack count
	 */
	fecti->base.stats.nss_nack = 0;

	/*
	 * Decelerate may have been attempted while we were accel pending.
	 * If decelerate is pending then we need to begin deceleration :-(
	 */
	if (!fecti->base.stats.decelerate_pending) {
		spin_unlock_bh(&fecti->lock);

		/*
		 * Release the connection.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	DEBUG_INFO("%p: Decelerate was pending\n", ci);
	fecti->base.stats.decelerate_pending = false;
	spin_unlock_bh(&fecti->lock);

	feci->decelerate(feci);

	/*
	 * Release the connection.
	 */
	feci->deref(feci);
	ecm_db_connection_deref(ci);
}

/*
 * ecm_front_end_ipv6_connection_tcp_front_end_accelerate()
 *	Accelerate a connection
 *
 * GGG TODO Refactor this function into a single function that np, udp and tcp
 * can all use and reduce the amount of code!
 */
static void ecm_front_end_ipv6_connection_tcp_front_end_accelerate(struct ecm_front_end_connection_instance *feci,
									struct ecm_classifier_process_response *pr,
									struct nf_conn *ct)
{
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;
	int32_t from_ifaces_first;
	int32_t to_ifaces_first;
	struct ecm_db_iface_instance *from_ifaces[ECM_DB_IFACE_HEIRARCHY_MAX];
	struct ecm_db_iface_instance *to_ifaces[ECM_DB_IFACE_HEIRARCHY_MAX];
	struct ecm_db_iface_instance *from_nss_iface;
	struct ecm_db_iface_instance *to_nss_iface;
	int32_t from_nss_iface_id;
	int32_t to_nss_iface_id;
	uint8_t from_nss_iface_address[ETH_ALEN];
	uint8_t to_nss_iface_address[ETH_ALEN];
	struct nss_ipv6_msg nim;
	struct nss_ipv6_rule_create_msg *nircm;
	struct ecm_classifier_instance *assignments[ECM_CLASSIFIER_TYPES];
	int aci_index;
	int assignment_count;
	nss_tx_status_t nss_tx_status;
	int32_t list_index;
	int32_t interface_type_counts[ECM_DB_IFACE_TYPE_COUNT];
	bool rule_invalid;
	ip_addr_t src_ip;
	ip_addr_t dest_ip;

	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);

	/*
	 * Can this connection be accelerated at all?
	 */
	DEBUG_INFO("%p: Accel conn: %p\n", fecti, fecti->ci);
	spin_lock_bh(&fecti->lock);
	if (fecti->accel_mode <= ECM_FRONT_END_ACCELERATION_MODE_FAIL_DENIED) {
		spin_unlock_bh(&fecti->lock);
		DEBUG_TRACE("%p: accel %p failed\n", fecti, fecti->ci);
		return;
	}

	/*
	 * If acceleration mode is anything other than "not accelerated" then ignore.
	 */
	if (fecti->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_DECEL) {
		spin_unlock_bh(&fecti->lock);
		DEBUG_TRACE("%p: Ignoring wrong mode accel for conn: %p\n", fecti, fecti->ci);
		return;
	}
	fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING;
	spin_unlock_bh(&fecti->lock);

	/*
	 * Okay construct an accel command.
	 * Initialise creation structure.
	 * NOTE: We leverage the app_data void pointer to be our 32 bit connection serial number.
	 * When we get it back we re-cast it to a uint32 and do a faster connection lookup.
	 */
	memset(&nim, 0, sizeof(struct nss_ipv6_msg));
	nss_cmn_msg_init(&nim.cm, NSS_IPV6_RX_INTERFACE, NSS_IPV6_TX_CREATE_RULE_MSG,
			sizeof(struct nss_ipv6_rule_create_msg),
			ecm_front_end_ipv6_connection_tcp_callback,
			(void *)ecm_db_connection_serial_get(fecti->ci));

	nircm = &nim.msg.rule_create;
	nircm->valid_flags = 0;
	nircm->rule_flags = 0;

	/*
	 * Initialize VLAN tag information
	 */
	nircm->vlan_primary_rule.ingress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_primary_rule.egress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_secondary_rule.ingress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_secondary_rule.egress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;

	/*
	 * Get the interface lists of the connection, we must have at least one interface in the list to continue
	 */
	from_ifaces_first = ecm_db_connection_from_interfaces_get_and_ref(fecti->ci, from_ifaces);
	if (from_ifaces_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
		DEBUG_WARN("%p: Accel attempt failed - no interfaces in from_interfaces list!\n", fecti);
		goto tcp_accel_bad_rule;
	}

	to_ifaces_first = ecm_db_connection_to_interfaces_get_and_ref(fecti->ci, to_ifaces);
	if (to_ifaces_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
		DEBUG_WARN("%p: Accel attempt failed - no interfaces in to_interfaces list!\n", fecti);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		goto tcp_accel_bad_rule;
	}

	/*
	 * First interface in each must be a known nss interface
	 */
	from_nss_iface = from_ifaces[from_ifaces_first];
	to_nss_iface = to_ifaces[to_ifaces_first];
	from_nss_iface_id = ecm_db_iface_nss_interface_identifier_get(from_nss_iface);
	to_nss_iface_id = ecm_db_iface_nss_interface_identifier_get(to_nss_iface);
	if ((from_nss_iface_id < 0) || (to_nss_iface_id < 0)) {
		DEBUG_TRACE("%p: from_nss_iface_id: %d, to_nss_iface_id: %d\n", fecti, from_nss_iface_id, to_nss_iface_id);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto tcp_accel_bad_rule;
	}

	/*
	 * New rule being created
	 */
	nircm->valid_flags |= NSS_IPV6_RULE_CREATE_CONN_VALID;

	/*
	 * Set interface numbers involved in accelerating this connection.
	 * These are the outer facing addresses from the heirarchy interface lists we got above.
	 * These may be overridden later if we detect special interface types e.g. ipsec.
	 */
	nircm->conn_rule.flow_interface_num = from_nss_iface_id;
	nircm->conn_rule.return_interface_num = to_nss_iface_id;

	/*
	 * We know that each outward facing interface is known to the NSS and so this connection could be accelerated.
	 * However the lists may also specify other interesting details that must be included in the creation command,
	 * for example, ethernet MAC, VLAN tagging or PPPoE session information.
	 * We get this information by walking from the outer to the innermost interface for each list and examine the interface types.
	 *
	 * Start with the 'from' (src) side.
	 * NOTE: The lists may contain a complex heirarchy of similar type of interface e.g. multiple vlans or tunnels within tunnels.
	 * This NSS cannot handle that - there is no way to describe this in the rule - if we see multiple types that would conflict we have to abort.
	 */
	DEBUG_TRACE("%p: Examine from/src heirarchy list\n", fecti);
	memset(interface_type_counts, 0, sizeof(interface_type_counts));
	rule_invalid = false;
	for (list_index = from_ifaces_first; !rule_invalid && (list_index < ECM_DB_IFACE_HEIRARCHY_MAX); list_index++) {
		struct ecm_db_iface_instance *ii;
		ecm_db_iface_type_t ii_type;
		char *ii_name;

		ii = from_ifaces[list_index];
		ii_type = ecm_db_connection_iface_type_get(ii);
		ii_name = ecm_db_interface_type_to_string(ii_type);
		DEBUG_TRACE("%p: list_index: %d, ii: %p, type: %d (%s)\n", fecti, list_index, ii, ii_type, ii_name);

		/*
		 * Extract information from this interface type if it is applicable to the rule.
		 * Conflicting information may cause accel to be unsupported.
		 */
		switch (ii_type) {
			struct ecm_db_interface_info_pppoe pppoe_info;
			struct ecm_db_interface_info_vlan vlan_info;
			uint32_t vlan_value = 0;
			struct net_device *vlan_in_dev = NULL;

		case ECM_DB_IFACE_TYPE_BRIDGE:
			DEBUG_TRACE("%p: Bridge\n", fecti);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Cannot cascade bridges
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: Bridge - ignore additional\n", fecti);
				break;
			}
			ecm_db_iface_bridge_address_get(ii, from_nss_iface_address);
			DEBUG_TRACE("%p: Bridge - mac: %pM\n", fecti, from_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_ETHERNET:
			DEBUG_TRACE("%p: Ethernet\n", fecti);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Ignore additional mac addresses, these are usually as a result of address propagation
				 * from bridges down to ports etc.
				 */
				DEBUG_TRACE("%p: Ethernet - ignore additional\n", fecti);
				break;
			}

			/*
			 * Can only handle one MAC, the first outermost mac.
			 */
			ecm_db_iface_ethernet_address_get(ii, from_nss_iface_address);
			DEBUG_TRACE("%p: Ethernet - mac: %pM\n", fecti, from_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_PPPOE:
			/*
			 * More than one PPPoE in the list is not valid!
			 */
			if (interface_type_counts[ii_type] != 0) {
				DEBUG_TRACE("%p: PPPoE - additional unsupported\n", fecti);
				rule_invalid = true;
				break;
			}

			/*
			 * Copy pppoe session info to the creation structure.
			 */
			ecm_db_iface_pppoe_session_info_get(ii, &pppoe_info);

			nircm->pppoe_rule.flow_pppoe_session_id = pppoe_info.pppoe_session_id;
			memcpy(nircm->pppoe_rule.flow_pppoe_remote_mac, pppoe_info.remote_mac, ETH_ALEN);
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_PPPOE_VALID;

			DEBUG_TRACE("%p: PPPoE - session: %x, mac: %pM\n", fecti,
					nircm->pppoe_rule.flow_pppoe_session_id,
					nircm->pppoe_rule.flow_pppoe_remote_mac);
			break;
		case ECM_DB_IFACE_TYPE_VLAN:
			DEBUG_TRACE("%p: VLAN\n", fecti);
			if (interface_type_counts[ii_type] > 1) {
				/*
				 * Can only support two vlans
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: VLAN - additional unsupported\n", fecti);
				break;
			}
			ecm_db_iface_vlan_info_get(ii, &vlan_info);
			vlan_value = ((vlan_info.vlan_tpid << 16) | vlan_info.vlan_tag);

			/*
			 * Look up the vlan device and incorporate the vlan priority into the vlan_value
			 */
			vlan_in_dev = dev_get_by_index(&init_net, ecm_db_iface_interface_identifier_get(ii));
			if (vlan_in_dev) {
				vlan_value |= vlan_dev_get_egress_prio(vlan_in_dev, pr->return_qos_tag);
				dev_put(vlan_in_dev);
				vlan_in_dev = NULL;
			}

			/*
			 * Primary or secondary (QinQ) VLAN?
			 */
			if (interface_type_counts[ii_type] == 0) {
				nircm->vlan_primary_rule.ingress_vlan_tag = vlan_value;
			} else {
				nircm->vlan_secondary_rule.ingress_vlan_tag = vlan_value;
			}
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_VLAN_VALID;

			/*
			 * If we have not yet got an ethernet mac then take this one (very unlikely as mac should have been propagated to the slave (outer) device
			 */
			if (interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET] == 0) {
				memcpy(from_nss_iface_address, vlan_info.address, ETH_ALEN);
				interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET]++;
				DEBUG_TRACE("%p: VLAN use mac: %pM\n", fecti, from_nss_iface_address);
			}
			DEBUG_TRACE("%p: vlan tag: %x\n", fecti, vlan_value);
			break;
		case ECM_DB_IFACE_TYPE_IPSEC_TUNNEL:
			DEBUG_TRACE("%p: IPSEC\n", fecti);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Can only support one ipsec
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: IPSEC - additional unsupported\n", fecti);
				break;
			}
			nircm->conn_rule.flow_interface_num = NSS_C2C_TX_INTERFACE;
			break;
		default:
			DEBUG_TRACE("%p: Ignoring: %d (%s)\n", fecti, ii_type, ii_name);
		}

		/*
		 * Seen an interface of this type
		 */
		interface_type_counts[ii_type]++;
	}
	if (rule_invalid) {
		DEBUG_WARN("%p: from/src Rule invalid\n", fecti);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto tcp_accel_bad_rule;
	}

	/*
	 * Now examine the TO / DEST heirarchy list to construct the destination part of the rule
	 */
	DEBUG_TRACE("%p: Examine to/dest heirarchy list\n", fecti);
	memset(interface_type_counts, 0, sizeof(interface_type_counts));
	rule_invalid = false;
	for (list_index = to_ifaces_first; !rule_invalid && (list_index < ECM_DB_IFACE_HEIRARCHY_MAX); list_index++) {
		struct ecm_db_iface_instance *ii;
		ecm_db_iface_type_t ii_type;
		char *ii_name;

		ii = to_ifaces[list_index];
		ii_type = ecm_db_connection_iface_type_get(ii);
		ii_name = ecm_db_interface_type_to_string(ii_type);
		DEBUG_TRACE("%p: list_index: %d, ii: %p, type: %d (%s)\n", fecti, list_index, ii, ii_type, ii_name);

		/*
		 * Extract information from this interface type if it is applicable to the rule.
		 * Conflicting information may cause accel to be unsupported.
		 */
		switch (ii_type) {
			struct ecm_db_interface_info_pppoe pppoe_info;
			struct ecm_db_interface_info_vlan vlan_info;
			uint32_t vlan_value = 0;
			struct net_device *vlan_out_dev = NULL;

		case ECM_DB_IFACE_TYPE_BRIDGE:
			DEBUG_TRACE("%p: Bridge\n", fecti);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Cannot cascade bridges
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: Bridge - ignore additional\n", fecti);
				break;
			}
			ecm_db_iface_bridge_address_get(ii, to_nss_iface_address);
			DEBUG_TRACE("%p: Bridge - mac: %pM\n", fecti, to_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_ETHERNET:
			DEBUG_TRACE("%p: Ethernet\n", fecti);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Ignore additional mac addresses, these are usually as a result of address propagation
				 * from bridges down to ports etc.
				 */
				DEBUG_TRACE("%p: Ethernet - ignore additional\n", fecti);
				break;
			}

			/*
			 * Can only handle one MAC, the first outermost mac.
			 */
			ecm_db_iface_ethernet_address_get(ii, to_nss_iface_address);
			DEBUG_TRACE("%p: Ethernet - mac: %pM\n", fecti, to_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_PPPOE:
			/*
			 * More than one PPPoE in the list is not valid!
			 */
			if (interface_type_counts[ii_type] != 0) {
				DEBUG_TRACE("%p: PPPoE - additional unsupported\n", fecti);
				rule_invalid = true;
				break;
			}

			/*
			 * Copy pppoe session info to the creation structure.
			 */
			ecm_db_iface_pppoe_session_info_get(ii, &pppoe_info);
			nircm->pppoe_rule.return_pppoe_session_id = pppoe_info.pppoe_session_id;
			memcpy(nircm->pppoe_rule.return_pppoe_remote_mac, pppoe_info.remote_mac, ETH_ALEN);
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_PPPOE_VALID;

			DEBUG_TRACE("%p: PPPoE - session: %x, mac: %pM\n", fecti,
				    nircm->pppoe_rule.return_pppoe_session_id,
				    nircm->pppoe_rule.return_pppoe_remote_mac);
			break;
		case ECM_DB_IFACE_TYPE_VLAN:
			DEBUG_TRACE("%p: VLAN\n", fecti);
			if (interface_type_counts[ii_type] > 1) {
				/*
				 * Can only support two vlans
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: VLAN - additional unsupported\n", fecti);
				break;
			}
			ecm_db_iface_vlan_info_get(ii, &vlan_info);
			vlan_value = ((vlan_info.vlan_tpid << 16) | vlan_info.vlan_tag);

			/*
			 * Look up the vlan device and incorporate the vlan priority into the vlan_value
			 */
			vlan_out_dev = dev_get_by_index(&init_net, ecm_db_iface_interface_identifier_get(ii));
			if (vlan_out_dev) {
				vlan_value |= vlan_dev_get_egress_prio(vlan_out_dev, pr->flow_qos_tag);
				dev_put(vlan_out_dev);
				vlan_out_dev = NULL;
			}

			/*
			 * Primary or secondary (QinQ) VLAN?
			 */
			if (interface_type_counts[ii_type] == 0) {
				nircm->vlan_primary_rule.egress_vlan_tag = vlan_value;
			} else {
				nircm->vlan_secondary_rule.egress_vlan_tag = vlan_value;
			}
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_VLAN_VALID;

			/*
			 * If we have not yet got an ethernet mac then take this one (very unlikely as mac should have been propagated to the slave (outer) device
			 */
			if (interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET] == 0) {
				memcpy(to_nss_iface_address, vlan_info.address, ETH_ALEN);
				interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET]++;
				DEBUG_TRACE("%p: VLAN use mac: %pM\n", fecti, to_nss_iface_address);
			}
			DEBUG_TRACE("%p: vlan tag: %x\n", fecti, vlan_value);
			break;
		case ECM_DB_IFACE_TYPE_IPSEC_TUNNEL:
			DEBUG_TRACE("%p: IPSEC\n", fecti);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Can only support one ipsec
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: IPSEC - additional unsupported\n", fecti);
				break;
			}
			nircm->conn_rule.return_interface_num = NSS_C2C_TX_INTERFACE;
			break;
		default:
			DEBUG_TRACE("%p: Ignoring: %d (%s)\n", fecti, ii_type, ii_name);
		}

		/*
		 * Seen an interface of this type
		 */
		interface_type_counts[ii_type]++;
	}
	if (rule_invalid) {
		DEBUG_WARN("%p: from/src Rule invalid\n", fecti);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto tcp_accel_bad_rule;
	}

	/*
	 * Routed or bridged?
	 */
	if (ecm_db_connection_is_routed_get(fecti->ci)) {
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_ROUTED;
	} else {
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_BRIDGE_FLOW;
	}

	/*
	 * Set up the flow and return qos tags
	 */
	nircm->qos_rule.flow_qos_tag = (uint32_t)pr->flow_qos_tag;
	nircm->qos_rule.return_qos_tag = (uint32_t)pr->return_qos_tag;
	nircm->valid_flags |= NSS_IPV6_RULE_CREATE_QOS_VALID;

	/*
	 * DSCP information?
	 */
	if (pr->process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP) {
		nircm->dscp_rule.flow_dscp = pr->flow_dscp;
		nircm->dscp_rule.return_dscp = pr->return_dscp;
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_DSCP_MARKING;
		nircm->valid_flags |= NSS_IPV6_RULE_CREATE_DSCP_MARKING_VALID;
	}

	/*
	 * Set protocol
	 */
	nircm->tuple.protocol = (int32_t)IPPROTO_TCP;

	/*
	 * The flow_ip is where the connection established from
	 */
	ecm_db_connection_from_address_get(fecti->ci, src_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nircm->tuple.flow_ip, src_ip);

	/*
	 * The dest_ip is where the connection is established to
	 */
	ecm_db_connection_to_address_get(fecti->ci, dest_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nircm->tuple.return_ip, dest_ip);

	/*
	 * Same approach as above for port information
	 */
	nircm->tuple.flow_ident = ecm_db_connection_from_port_get(fecti->ci);
	nircm->tuple.return_ident = ecm_db_connection_to_port_get(fecti->ci);

	/*
	 * Get mac addresses.
	 * The src_mac is the mac address of the node that established the connection.
	 * This will work whether the from_node is LAN (egress) or WAN (ingress).
	 */
	ecm_db_connection_from_node_address_get(fecti->ci, (uint8_t *)nircm->conn_rule.flow_mac);

	/*
	 * The dest_mac is more complex.  For egress it is the node address of the 'to' side of the connection.
	 * For ingress it is the node adress of the NAT'ed 'to' IP.
	 * Essentially it is the MAC of node associated with create.dest_ip and this is "to nat" side.
	 */
	ecm_db_connection_to_node_address_get(fecti->ci, (uint8_t *)nircm->conn_rule.return_mac);

	/*
	 * Get MTU information
	 */
	nircm->conn_rule.flow_mtu = (uint32_t)ecm_db_connection_from_iface_mtu_get(fecti->ci);
	nircm->conn_rule.return_mtu = (uint32_t)ecm_db_connection_to_iface_mtu_get(fecti->ci);

	/*
	 * Need window scaling information from conntrack if available
	 * Start by looking up the conntrack connection
	 */
	if (!ct) {
		/*
		 * No conntrack so no need to check window sequence space
		 */
		DEBUG_TRACE("%p: TCP Accel no ct from conn %p to get window data\n", fecti, fecti->ci);
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_NO_SEQ_CHECK;
	} else {
		spin_lock_bh(&ct->lock);
		DEBUG_TRACE("%p: TCP Accel Get window data from ct %p for conn %p\n", fecti, ct, fecti->ci);

		nircm->tcp_rule.flow_window_scale = ct->proto.tcp.seen[0].td_scale;
		nircm->tcp_rule.flow_max_window = ct->proto.tcp.seen[0].td_maxwin;
		nircm->tcp_rule.flow_end = ct->proto.tcp.seen[0].td_end;
		nircm->tcp_rule.flow_max_end = ct->proto.tcp.seen[0].td_maxend;
		nircm->tcp_rule.return_window_scale = ct->proto.tcp.seen[1].td_scale;
		nircm->tcp_rule.return_max_window = ct->proto.tcp.seen[1].td_maxwin;
		nircm->tcp_rule.return_end = ct->proto.tcp.seen[1].td_end;
		nircm->tcp_rule.return_max_end = ct->proto.tcp.seen[1].td_maxend;
		if (nf_ct_tcp_be_liberal || nf_ct_tcp_no_window_check
				|| (ct->proto.tcp.seen[0].flags & IP_CT_TCP_FLAG_BE_LIBERAL)
				|| (ct->proto.tcp.seen[1].flags & IP_CT_TCP_FLAG_BE_LIBERAL)) {
			nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_NO_SEQ_CHECK;
		}
		spin_unlock_bh(&ct->lock);
	}

	nircm->valid_flags |= NSS_IPV6_RULE_CREATE_TCP_VALID;

	/*
	 * Sync our creation command from the assigned classifiers to get specific additional creation rules.
	 * NOTE: These are called in ascending order of priority and so the last classifier (highest) shall
	 * override any preceding classifiers.
	 * This also gives the classifiers a chance to see that acceleration is being attempted.
	 */
	assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(fecti->ci, assignments);
	for (aci_index = 0; aci_index < assignment_count; ++aci_index) {
		struct ecm_classifier_instance *aci;

		aci = assignments[aci_index];
		DEBUG_TRACE("%p: sync from: %p, type: %d\n", fecti, aci, aci->type_get(aci));
		aci->sync_from_v6(aci, nircm);
	}
	ecm_db_connection_assignments_release(assignment_count, assignments);

	/*
	 * Release the interface lists
	 */
	ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
	ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);

	DEBUG_INFO("%p: TCP Accelerate connection %p\n"
			"Protocol: %d\n"
			"from_mtu: %u\n"
			"to_mtu: %u\n"
			"from_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"to_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"from_mac: %pM\n"
			"to_mac: %pM\n"
			"src_iface_num: %u\n"
			"dest_iface_num: %u\n"
			"ingress_inner_vlan_tag: %u\n"
			"egress_inner_vlan_tag: %u\n"
			"ingress_outer_vlan_tag: %u\n"
			"egress_outer_vlan_tag: %u\n"
			"rule_flags: %x\n"
			"valid_flags: %x\n"
			"return_pppoe_session_id: %u\n"
			"return_pppoe_remote_mac: %pM\n"
			"flow_pppoe_session_id: %u\n"
			"flow_pppoe_remote_mac: %pM\n"
			"flow_qos_tag: %x (%u)\n"
			"return_qos_tag: %x (%u)\n"
			"flow_window_scale: %u\n"
			"flow_max_window: %u\n"
			"flow_end: %u\n"
			"flow_max_end: %u\n"
			"return_window_scale: %u\n"
			"return_max_window: %u\n"
			"return_end: %u\n"
			"return_max_end: %u\n"
			"flow_dscp: %x\n"
			"return_dscp: %x\n",
			fecti,
			fecti->ci,
			nircm->tuple.protocol,
			nircm->conn_rule.flow_mtu,
			nircm->conn_rule.return_mtu,
			ECM_IP_ADDR_TO_OCTAL(src_ip), nircm->tuple.flow_ident,
			ECM_IP_ADDR_TO_OCTAL(dest_ip), nircm->tuple.return_ident,
			nircm->conn_rule.flow_mac,
			nircm->conn_rule.return_mac,
			nircm->conn_rule.flow_interface_num,
			nircm->conn_rule.return_interface_num,
			nircm->vlan_primary_rule.ingress_vlan_tag,
			nircm->vlan_primary_rule.egress_vlan_tag,
			nircm->vlan_secondary_rule.ingress_vlan_tag,
			nircm->vlan_secondary_rule.egress_vlan_tag,
			nircm->rule_flags,
			nircm->valid_flags,
			nircm->pppoe_rule.return_pppoe_session_id,
			nircm->pppoe_rule.return_pppoe_remote_mac,
			nircm->pppoe_rule.flow_pppoe_session_id,
			nircm->pppoe_rule.flow_pppoe_remote_mac,
			nircm->qos_rule.flow_qos_tag, nircm->qos_rule.flow_qos_tag,
			nircm->qos_rule.return_qos_tag, nircm->qos_rule.return_qos_tag,
			nircm->tcp_rule.flow_window_scale,
			nircm->tcp_rule.flow_max_window,
			nircm->tcp_rule.flow_end,
			nircm->tcp_rule.flow_max_end,
			nircm->tcp_rule.return_window_scale,
			nircm->tcp_rule.return_max_window,
			nircm->tcp_rule.return_end,
			nircm->tcp_rule.return_max_end,
			nircm->dscp_rule.flow_dscp,
			nircm->dscp_rule.return_dscp);

	/*
	 * Call the rule create function
	 */
	nss_tx_status = nss_ipv6_tx(ecm_front_end_ipv6_nss_ipv6_mgr, &nim);
	if (nss_tx_status == NSS_TX_SUCCESS) {
		spin_lock_bh(&fecti->lock);
		DEBUG_ASSERT(fecti->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecti, fecti->accel_mode);
		fecti->base.stats.driver_fail = 0;		/* Reset */
		spin_unlock_bh(&fecti->lock);
		return;
	}

	/*
	 * TX failed
	 */
	spin_lock_bh(&fecti->lock);
	DEBUG_ASSERT(fecti->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecti, fecti->accel_mode);
	fecti->base.stats.driver_fail_total++;
	fecti->base.stats.driver_fail++;
	if (fecti->base.stats.driver_fail >= fecti->base.stats.driver_fail_limit) {
		DEBUG_WARN("%p: Accel failed - driver fail limit\n", fecti);
		fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DRIVER;
	}
	spin_unlock_bh(&fecti->lock);
	return;

tcp_accel_bad_rule:
	;

	/*
	 * Jump to here when rule data is bad and an offload command cannot be constructed
	 */
	DEBUG_WARN("%p: Accel failed - bad rule\n", fecti);
	spin_lock_bh(&fecti->lock);
	DEBUG_ASSERT(fecti->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecti, fecti->accel_mode);
	fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_RULE;
	spin_unlock_bh(&fecti->lock);
}

/*
 * ecm_front_end_ipv6_connection_tcp_destroy_callback()
 *	Callback for handling create ack/nack calls.
 */
static void ecm_front_end_ipv6_connection_tcp_destroy_callback(void *app_data, struct nss_ipv6_msg *nim)
{
	struct nss_ipv6_rule_destroy_msg *nirdm = &nim->msg.rule_destroy;
	uint32_t serial = (uint32_t)app_data;
	struct ecm_db_connection_instance *ci;
	struct ecm_front_end_connection_instance *feci;
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti;
	ip_addr_t flow_ip;
	ip_addr_t return_ip;

	/*
	 * Is this a response to a destroy message?
	 */
	if (nim->cm.type != NSS_IPV6_TX_DESTROY_RULE_MSG) {
		DEBUG_ERROR("%p: tcp destroy callback with improper type: %d\n", nim, nim->cm.type);
		return;
	}

	/*
	 * Look up ecm connection so that we can update the status.
	 */
	ci = ecm_db_connection_serial_find_and_ref(serial);
	if (!ci) {
		DEBUG_TRACE("%p: destroy callback, connection not found, serial: %u\n", nim, serial);
		return;
	}

	/*
	 * Get the front end instance
	 */
	feci = ecm_db_connection_front_end_get_and_ref(ci);
	fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;
	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);

	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(flow_ip, nirdm->tuple.flow_ip);
	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(return_ip, nirdm->tuple.return_ip);

	/*
	 * Dump some useful trace information.
	 */
	DEBUG_TRACE("%p: decelerate response for connection: %p\n", fecti, fecti->ci);
	DEBUG_TRACE("%p: flow_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecti, ECM_IP_ADDR_TO_OCTAL(flow_ip), nirdm->tuple.flow_ident);
	DEBUG_TRACE("%p: return_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecti, ECM_IP_ADDR_TO_OCTAL(return_ip), nirdm->tuple.return_ident);
	DEBUG_TRACE("%p: protocol: %d\n", fecti, nirdm->tuple.protocol);

	spin_lock_bh(&fecti->lock);

	/*
	 * If decel is not still pending then it's possible that the NSS ended acceleration by some other reason e.g. flush
	 * In which case we cannot rely on the response we get here.
	 */
	if (fecti->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_DECEL_PENDING) {
		spin_unlock_bh(&fecti->lock);

		/*
		 * Release the connections.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	DEBUG_TRACE("%p: response: %d\n", fecti, nim->cm.response);
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DECEL;
	} else {
		fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
	}
	spin_unlock_bh(&fecti->lock);

	/*
	 * TCP acceleration ends
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_tcp_accelerated_count--;	/* Protocol specific counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_tcp_accelerated_count >= 0, "Bad udp accel counter\n");
	ecm_front_end_ipv6_accelerated_count--;		/* General running counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_accelerated_count >= 0, "Bad accel counter\n");
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Release the connections.
	 */
	feci->deref(feci);
	ecm_db_connection_deref(ci);
}

/*
 * ecm_front_end_ipv6_connection_tcp_front_end_decelerate()
 *	Decelerate a connection
 */
static void ecm_front_end_ipv6_connection_tcp_front_end_decelerate(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;
	struct nss_ipv6_msg nim;
	struct nss_ipv6_rule_destroy_msg *nirdm;
	ip_addr_t src_ip;
	ip_addr_t dest_ip;
	nss_tx_status_t nss_tx_status;

	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);

	/*
	 * If decelerate is in error or already pending then ignore
	 */
	spin_lock_bh(&fecti->lock);
	if (fecti->base.stats.decelerate_pending) {
		spin_unlock_bh(&fecti->lock);
		return;
	}

	/*
	 * If acceleration is pending then we cannot decelerate right now or we will race with it
	 * Set a decelerate pending flag that will be actioned when the acceleration command is complete.
	 */
	if (fecti->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING) {
		fecti->base.stats.decelerate_pending = true;
		spin_unlock_bh(&fecti->lock);
		return;
	}

	/*
	 * Can only decelerate if accelerated
	 * NOTE: This will also deny accel when the connection is in fail condition too.
	 */
	if (fecti->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_ACCEL) {
		spin_unlock_bh(&fecti->lock);
		return;
	}

	/*
	 * Initiate deceleration
	 */
	fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL_PENDING;
	spin_unlock_bh(&fecti->lock);

	/*
	 * Prepare deceleration message
	 */
	nss_cmn_msg_init(&nim.cm, NSS_IPV6_RX_INTERFACE, NSS_IPV6_TX_DESTROY_RULE_MSG,
			sizeof(struct nss_ipv6_rule_destroy_msg),
			ecm_front_end_ipv6_connection_tcp_destroy_callback,
			(void *)ecm_db_connection_serial_get(fecti->ci));

	nirdm = &nim.msg.rule_destroy;
	nirdm->tuple.protocol = (int32_t)IPPROTO_TCP;

	/*
	 * Get addressing information
	 */
	ecm_db_connection_from_address_get(fecti->ci, src_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nirdm->tuple.flow_ip, src_ip);
	ecm_db_connection_to_address_get(fecti->ci, dest_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nirdm->tuple.return_ip, dest_ip);
	nirdm->tuple.flow_ident = ecm_db_connection_from_port_get(fecti->ci);
	nirdm->tuple.return_ident = ecm_db_connection_to_port_get(fecti->ci);

	DEBUG_INFO("%p: TCP Connection %p decelerate\n"
			"src_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"dest_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n",
			fecti, fecti->ci,
			ECM_IP_ADDR_TO_OCTAL(src_ip), nirdm->tuple.flow_ident,
			ECM_IP_ADDR_TO_OCTAL(dest_ip), nirdm->tuple.return_ident);

	/*
	 * Destroy the NSS connection cache entry.
	 */
	nss_tx_status = nss_ipv6_tx(ecm_front_end_ipv6_nss_ipv6_mgr, &nim);
	if (nss_tx_status == NSS_TX_SUCCESS) {
		spin_lock_bh(&fecti->lock);
		fecti->base.stats.driver_fail = 0;		/* Reset */
		spin_unlock_bh(&fecti->lock);
		return;
	}

	/*
	 * TX failed
	 */
	spin_lock_bh(&fecti->lock);
	fecti->base.stats.driver_fail_total++;
	fecti->base.stats.driver_fail++;
	if (fecti->base.stats.driver_fail >= fecti->base.stats.driver_fail_limit) {
		DEBUG_WARN("%p: Decel failed - driver fail limit\n", fecti);
		fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DRIVER;
	}
	spin_unlock_bh(&fecti->lock);
}

/*
 * ecm_front_end_ipv6_connection_tcp_front_end_accel_state_get()
 *	Get acceleration state
 */
static ecm_front_end_acceleration_mode_t ecm_front_end_ipv6_connection_tcp_front_end_accel_state_get(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;
	ecm_classifier_acceleration_mode_t state;

	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);
	spin_lock_bh(&fecti->lock);
	state = fecti->accel_mode;
	spin_unlock_bh(&fecti->lock);
	return state;
}

/*
 * ecm_front_end_ipv6_connection_tcp_front_end_action_seen()
 *	Acceleration action / activity has been seen for this connection.
 *
 * NOTE: Call the action_seen() method when the NSS has demonstrated that it has offloaded some data for a connection.
 */
static void ecm_front_end_ipv6_connection_tcp_front_end_action_seen(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);
	DEBUG_INFO("%p: Action seen\n", fecti);
	spin_lock_bh(&fecti->lock);
	fecti->base.stats.no_action_seen = 0;
	spin_unlock_bh(&fecti->lock);
}

/*
 * ecm_front_end_ipv6_connection_tcp_front_end_accel_ceased()
 *	NSS has indicated that acceleration has stopped.
 *
 * NOTE: This is called in response to an NSS self-initiated termination of acceleration.
 * This must NOT be called because the ECM terminated the acceleration.
 */
static void ecm_front_end_ipv6_connection_tcp_front_end_accel_ceased(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);
	DEBUG_INFO("%p: accel ceased\n", fecti);

	/*
	 * If connection is no longer accelerated by the time we get here just ignore the command
	 */
	spin_lock_bh(&fecti->lock);
	if (fecti->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_ACCEL) {
		spin_unlock_bh(&fecti->lock);
		return;
	}

	/*
	 * If the no_action_seen counter was not reset then acceleration ended without any offload action
	 */
	if (fecti->base.stats.no_action_seen) {
		fecti->base.stats.no_action_seen_total++;
	}

	/*
	 * If the no_action_seen indicates successive cessations of acceleration without any offload action occuring
	 * then we fail out this connection
	 */
	if (fecti->base.stats.no_action_seen >= fecti->base.stats.no_action_seen_limit) {
		fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_NO_ACTION;
	} else {
		fecti->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
	}
	spin_unlock_bh(&fecti->lock);

	/*
	 * TCP acceleration ends
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_tcp_accelerated_count--;	/* Protocol specific counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_tcp_accelerated_count >= 0, "Bad tcp accel counter\n");
	ecm_front_end_ipv6_accelerated_count--;		/* General running counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_accelerated_count >= 0, "Bad accel counter\n");
	spin_unlock_bh(&ecm_front_end_ipv6_lock);
}

/*
 * ecm_front_end_ipv6_connection_tcp_front_end_ref()
 *	Ref a connection front end instance
 */
static void ecm_front_end_ipv6_connection_tcp_front_end_ref(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);
	spin_lock_bh(&fecti->lock);
	fecti->refs++;
	DEBUG_TRACE("%p: fecti ref %d\n", fecti, fecti->refs);
	DEBUG_ASSERT(fecti->refs > 0, "%p: ref wrap\n", fecti);
	spin_unlock_bh(&fecti->lock);
}

/*
 * ecm_front_end_ipv6_connection_tcp_front_end_deref()
 *	Deref a connection front end instance
 */
static int ecm_front_end_ipv6_connection_tcp_front_end_deref(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);

	spin_lock_bh(&fecti->lock);
	fecti->refs--;
	DEBUG_ASSERT(fecti->refs >= 0, "%p: ref wrap\n", fecti);

	if (fecti->refs > 0) {
		int refs = fecti->refs;
		spin_unlock_bh(&fecti->lock);
		DEBUG_TRACE("%p: fecti deref %d\n", fecti, refs);
		return refs;
	}
	spin_unlock_bh(&fecti->lock);

	/*
	 * We can now destroy the instance
	 */
	DEBUG_TRACE("%p: fecti final\n", fecti);
	DEBUG_CLEAR_MAGIC(fecti);
	kfree(fecti);

	return 0;
}

/*
 * ecm_front_end_ipv6_connection_tcp_front_end_xml_state_get()
 *	Return an XML element containing the state of this TCP front end instance
 */
static int ecm_front_end_ipv6_connection_tcp_front_end_xml_state_get(struct ecm_front_end_connection_instance *feci, char *buf, int buf_sz)
{
	bool can_accel;
	ecm_front_end_acceleration_mode_t accel_mode;
	struct ecm_front_end_connection_mode_stats stats;
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC, "%p: magic failed", fecti);

	spin_lock_bh(&fecti->lock);
	can_accel = fecti->can_accel;
	accel_mode = fecti->accel_mode;
	memcpy(&stats, &feci->stats, sizeof(struct ecm_front_end_connection_mode_stats));
	spin_unlock_bh(&fecti->lock);

	return snprintf(buf, buf_sz, "<front_end_tcp can_accel=\"%d\" accel_mode=\"%d\" decelerate_pending=\"%d\""
			" no_action_seen_total=\"%d\" no_action_seen=\"%d\" no_action_seen_limit=\"%d\""
			" driver_fail_total=\"%d\" driver_fail=\"%d\" driver_fail_limit=\"%d\""
			" nss_nack_total=\"%d\" nss_nack=\"%d\" nss_nack_limit=\"%d\"/>\n",
			can_accel,
			accel_mode,
			stats.decelerate_pending,
			stats.no_action_seen_total,
			stats.no_action_seen,
			stats.no_action_seen_limit,
			stats.driver_fail_total,
			stats.driver_fail,
			stats.driver_fail_limit,
			stats.nss_nack_total,
			stats.nss_nack,
			stats.nss_nack_limit);
}

/*
 * ecm_front_end_ipv6_connection_tcp_instance_alloc()
 *	Create a front end instance specific for TCP connection
 */
static struct ecm_front_end_ipv6_connection_tcp_instance *ecm_front_end_ipv6_connection_tcp_instance_alloc(
								struct ecm_db_connection_instance *ci,
								struct ecm_db_mapping_instance *src_mi,
								struct ecm_db_mapping_instance *dest_mi,
								bool can_accel)
{
	struct ecm_front_end_ipv6_connection_tcp_instance *fecti;

	fecti = (struct ecm_front_end_ipv6_connection_tcp_instance *)kzalloc(sizeof(struct ecm_front_end_ipv6_connection_tcp_instance), GFP_ATOMIC | __GFP_NOWARN);
	if (!fecti) {
		DEBUG_WARN("TCP Front end alloc failed\n");
		return NULL;
	}

	/*
	 * Refs is 1 for the creator of the connection
	 */
	fecti->refs = 1;
	DEBUG_SET_MAGIC(fecti, ECM_FRONT_END_IPV6_CONNECTION_TCP_INSTANCE_MAGIC);
	spin_lock_init(&fecti->lock);

	fecti->can_accel = can_accel;
	fecti->accel_mode = (can_accel) ? ECM_FRONT_END_ACCELERATION_MODE_DECEL : ECM_FRONT_END_ACCELERATION_MODE_FAIL_DENIED;
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	fecti->base.stats.no_action_seen_limit = ecm_front_end_ipv6_no_action_limit_default;
	fecti->base.stats.driver_fail_limit = ecm_front_end_ipv6_driver_fail_limit_default;
	fecti->base.stats.nss_nack_limit = ecm_front_end_ipv6_nack_limit_default;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Copy reference to connection - no need to ref ci as ci maintains a ref to this instance instead (this instance persists for as long as ci does)
	 */
	fecti->ci = ci;

	/*
	 * Populate the methods and callbacks
	 */
	fecti->base.ref = ecm_front_end_ipv6_connection_tcp_front_end_ref;
	fecti->base.deref = ecm_front_end_ipv6_connection_tcp_front_end_deref;
	fecti->base.decelerate = ecm_front_end_ipv6_connection_tcp_front_end_decelerate;
	fecti->base.accel_state_get = ecm_front_end_ipv6_connection_tcp_front_end_accel_state_get;
	fecti->base.action_seen = ecm_front_end_ipv6_connection_tcp_front_end_action_seen;
	fecti->base.accel_ceased = ecm_front_end_ipv6_connection_tcp_front_end_accel_ceased;
	fecti->base.xml_state_get = ecm_front_end_ipv6_connection_tcp_front_end_xml_state_get;

	return fecti;
}

/*
 * ecm_front_end_ipv6_connection_udp_callback()
 *	Callback for handling create ack/nack calls.
 *
 * TODO: Since we don't want to hold data structure reference across multiple heterogenous
 * CPUs, we should use the app_data as the sequence # and do a fast look up rather than
 * a 5-tuple lookup.
 */
static void ecm_front_end_ipv6_connection_udp_callback(void *app_data, struct nss_ipv6_msg *nim)
{
	struct nss_ipv6_rule_create_msg *nircm = &nim->msg.rule_create;
	uint32_t serial = (uint32_t)app_data;
	struct ecm_db_connection_instance *ci;
	struct ecm_front_end_connection_instance *feci;
	struct ecm_front_end_ipv6_connection_udp_instance *fecui;
	ip_addr_t flow_ip;
	ip_addr_t return_ip;

	/*
	 * Is this a response to a create message?
	 */
	if (nim->cm.type != NSS_IPV6_TX_CREATE_RULE_MSG) {
		DEBUG_ERROR("%p: udp create callback with improper type: %d, serial: %u\n", nim, nim->cm.type, serial);
		return;
	}

	/*
	 * Look up ecm connection so that we can update the status.
	 */
	ci = ecm_db_connection_serial_find_and_ref(serial);
	if (!ci) {
		DEBUG_TRACE("%p: create callback, connection not found, serial: %u\n", nim, serial);
		return;
	}

	/*
	 * Get the front end instance
	 */
	feci = ecm_db_connection_front_end_get_and_ref(ci);
	fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;
	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);

	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(flow_ip, nircm->tuple.flow_ip);
	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(return_ip, nircm->tuple.return_ip);

	/*
	 * Dump some useful trace information.
	 */
	DEBUG_TRACE("%p: accelerate response for connection: %p, serial: %u\n", fecui, fecui->ci, serial);
	DEBUG_TRACE("%p: rule_flags: %x, valid_flags: %x\n", fecui, nircm->rule_flags, nircm->valid_flags);
	DEBUG_TRACE("%p: flow_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecui, ECM_IP_ADDR_TO_OCTAL(flow_ip), nircm->tuple.flow_ident);
	DEBUG_TRACE("%p: return_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecui, ECM_IP_ADDR_TO_OCTAL(return_ip), nircm->tuple.return_ident);
	DEBUG_TRACE("%p: protocol: %d\n", fecui, nircm->tuple.protocol);

	/*
	 * Handle the creation result code.
	 */
	DEBUG_TRACE("%p: response: %d\n", fecui, nim->cm.response);
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		/*
		 * Creation command failed (specific reason ignored).
		 */
		DEBUG_TRACE("%p: accel nack: %d\n", fecui, nim->cm.error);
		spin_lock_bh(&fecui->lock);
		DEBUG_ASSERT(fecui->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Unexpected mode: %d\n", ci, fecui->accel_mode);
		fecui->base.stats.nss_nack++;
		fecui->base.stats.nss_nack_total++;
		if (fecui->base.stats.nss_nack >= fecui->base.stats.nss_nack_limit) {
			/*
			 * Too many NSS rejections
			 */
			fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_NSS;
		} else {
			/*
			 * Revert to decelerated
			 */
			fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
		}

		/*
		 * Clear any decelerate pending flag since we aren't accelerated anyway we can just clear this whether it is set or not
		 */
		fecui->base.stats.decelerate_pending = false;
		spin_unlock_bh(&fecui->lock);

		/*
		 * Release the connection.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	/*
	 * Create succeeded, declare that we are accelerated.
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_udp_accelerated_count++;	/* Protocol specific counter */
	ecm_front_end_ipv6_accelerated_count++;		/* General running counter */
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	spin_lock_bh(&fecui->lock);
	DEBUG_ASSERT(fecui->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Unexpected mode: %d\n", ci, fecui->accel_mode);
	fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_ACCEL;

	/*
	 * Increement the no-action counter, this is reset if offload action is seen
	 */
	fecui->base.stats.no_action_seen++;

	/*
	 * Clear any nack count
	 */
	fecui->base.stats.nss_nack = 0;

	/*
	 * Decelerate may have been attempted while we were accel pending.
	 * If decelerate is pending then we need to begin deceleration :-(
	 */
	if (!fecui->base.stats.decelerate_pending) {
		spin_unlock_bh(&fecui->lock);

		/*
		 * Release the connection.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	DEBUG_INFO("%p: Decelerate was pending\n", ci);
	fecui->base.stats.decelerate_pending = false;
	spin_unlock_bh(&fecui->lock);

	feci->decelerate(feci);

	/*
	 * Release the connection.
	 */
	feci->deref(feci);
	ecm_db_connection_deref(ci);
}

/*
 * ecm_front_end_ipv6_connection_udp_front_end_accelerate()
 *	Accelerate a connection
 *
 * GGG TODO Refactor this function into a single function that np, udp and tcp
 * can all use and reduce the amount of code!
 */
static void ecm_front_end_ipv6_connection_udp_front_end_accelerate(struct ecm_front_end_connection_instance *feci,
									struct ecm_classifier_process_response *pr)
{
	struct ecm_front_end_ipv6_connection_udp_instance *fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;
	int32_t from_ifaces_first;
	int32_t to_ifaces_first;
	struct ecm_db_iface_instance *from_ifaces[ECM_DB_IFACE_HEIRARCHY_MAX];
	struct ecm_db_iface_instance *to_ifaces[ECM_DB_IFACE_HEIRARCHY_MAX];
	struct ecm_db_iface_instance *from_nss_iface;
	struct ecm_db_iface_instance *to_nss_iface;
	int32_t from_nss_iface_id;
	int32_t to_nss_iface_id;
	uint8_t from_nss_iface_address[ETH_ALEN];
	uint8_t to_nss_iface_address[ETH_ALEN];
	struct nss_ipv6_msg nim;
	struct nss_ipv6_rule_create_msg *nircm;
	struct ecm_classifier_instance *assignments[ECM_CLASSIFIER_TYPES];
	int aci_index;
	int assignment_count;
	nss_tx_status_t nss_tx_status;
	int32_t list_index;
	int32_t interface_type_counts[ECM_DB_IFACE_TYPE_COUNT];
	bool rule_invalid;
	ip_addr_t src_ip;
	ip_addr_t dest_ip;

	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);

	/*
	 * Can this connection be accelerated at all?
	 */
	DEBUG_INFO("%p: Accel conn: %p\n", fecui, fecui->ci);
	spin_lock_bh(&fecui->lock);
	if (fecui->accel_mode <= ECM_FRONT_END_ACCELERATION_MODE_FAIL_DENIED) {
		spin_unlock_bh(&fecui->lock);
		DEBUG_TRACE("%p: accel %p failed\n", fecui, fecui->ci);
		return;
	}

	/*
	 * If acceleration mode is anything other than "not accelerated" then ignore.
	 */
	if (fecui->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_DECEL) {
		spin_unlock_bh(&fecui->lock);
		DEBUG_TRACE("%p: Ignoring wrong mode accel for conn: %p\n", fecui, fecui->ci);
		return;
	}
	fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING;
	spin_unlock_bh(&fecui->lock);

	/*
	 * Okay construct an accel command.
	 * Initialise creation structure.
	 * NOTE: We leverage the app_data void pointer to be our 32 bit connection serial number.
	 * When we get it back we re-cast it to a uint32 and do a faster connection lookup.
	 */
	memset(&nim, 0, sizeof(struct nss_ipv6_msg));
	nss_cmn_msg_init(&nim.cm, NSS_IPV6_RX_INTERFACE, NSS_IPV6_TX_CREATE_RULE_MSG,
			sizeof(struct nss_ipv6_rule_create_msg),
			ecm_front_end_ipv6_connection_udp_callback,
			(void *)ecm_db_connection_serial_get(fecui->ci));

	nircm = &nim.msg.rule_create;
	nircm->valid_flags = 0;
	nircm->rule_flags = 0;

	/*
	 * Initialize VLAN tag information
	 */
	nircm->vlan_primary_rule.ingress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_primary_rule.egress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_secondary_rule.ingress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_secondary_rule.egress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;

	/*
	 * Get the interface lists of the connection, we must have at least one interface in the list to continue
	 */
	from_ifaces_first = ecm_db_connection_from_interfaces_get_and_ref(fecui->ci, from_ifaces);
	if (from_ifaces_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
		DEBUG_WARN("%p: Accel attempt failed - no interfaces in from_interfaces list!\n", fecui);
		goto udp_accel_bad_rule;
	}

	to_ifaces_first = ecm_db_connection_to_interfaces_get_and_ref(fecui->ci, to_ifaces);
	if (to_ifaces_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
		DEBUG_WARN("%p: Accel attempt failed - no interfaces in to_interfaces list!\n", fecui);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		goto udp_accel_bad_rule;
	}

	/*
	 * First interface in each must be a known nss interface
	 */
	from_nss_iface = from_ifaces[from_ifaces_first];
	to_nss_iface = to_ifaces[to_ifaces_first];
	from_nss_iface_id = ecm_db_iface_nss_interface_identifier_get(from_nss_iface);
	to_nss_iface_id = ecm_db_iface_nss_interface_identifier_get(to_nss_iface);
	if ((from_nss_iface_id < 0) || (to_nss_iface_id < 0)) {
		DEBUG_TRACE("%p: from_nss_iface_id: %d, to_nss_iface_id: %d\n", fecui, from_nss_iface_id, to_nss_iface_id);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto udp_accel_bad_rule;
	}

	/*
	 * New rule being created
	 */
	nircm->valid_flags |= NSS_IPV6_RULE_CREATE_CONN_VALID;

	/*
	 * Set interface numbers involved in accelerating this connection.
	 * These are the outer facing addresses from the heirarchy interface lists we got above.
	 * These may be overridden later if we detect special interface types e.g. ipsec.
	 */
	nircm->conn_rule.flow_interface_num = from_nss_iface_id;
	nircm->conn_rule.return_interface_num = to_nss_iface_id;

	/*
	 * We know that each outward facing interface is known to the NSS and so this connection could be accelerated.
	 * However the lists may also specify other interesting details that must be included in the creation command,
	 * for example, ethernet MAC, VLAN tagging or PPPoE session information.
	 * We get this information by walking from the outer to the innermost interface for each list and examine the interface types.
	 *
	 * Start with the 'from' (src) side.
	 * NOTE: The lists may contain a complex heirarchy of similar type of interface e.g. multiple vlans or tunnels within tunnels.
	 * This NSS cannot handle that - there is no way to describe this in the rule - if we see multiple types that would conflict we have to abort.
	 */
	DEBUG_TRACE("%p: Examine from/src heirarchy list\n", fecui);
	memset(interface_type_counts, 0, sizeof(interface_type_counts));
	rule_invalid = false;
	for (list_index = from_ifaces_first; !rule_invalid && (list_index < ECM_DB_IFACE_HEIRARCHY_MAX); list_index++) {
		struct ecm_db_iface_instance *ii;
		ecm_db_iface_type_t ii_type;
		char *ii_name;

		ii = from_ifaces[list_index];
		ii_type = ecm_db_connection_iface_type_get(ii);
		ii_name = ecm_db_interface_type_to_string(ii_type);
		DEBUG_TRACE("%p: list_index: %d, ii: %p, type: %d (%s)\n", fecui, list_index, ii, ii_type, ii_name);

		/*
		 * Extract information from this interface type if it is applicable to the rule.
		 * Conflicting information may cause accel to be unsupported.
		 */
		switch (ii_type) {
			struct ecm_db_interface_info_pppoe pppoe_info;
			struct ecm_db_interface_info_vlan vlan_info;
			uint32_t vlan_value = 0;
			struct net_device *vlan_in_dev = NULL;

		case ECM_DB_IFACE_TYPE_BRIDGE:
			DEBUG_TRACE("%p: Bridge\n", fecui);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Cannot cascade bridges
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: Bridge - ignore additional\n", fecui);
				break;
			}
			ecm_db_iface_bridge_address_get(ii, from_nss_iface_address);
			DEBUG_TRACE("%p: Bridge - mac: %pM\n", fecui, from_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_ETHERNET:
			DEBUG_TRACE("%p: Ethernet\n", fecui);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Ignore additional mac addresses, these are usually as a result of address propagation
				 * from bridges down to ports etc.
				 */
				DEBUG_TRACE("%p: Ethernet - ignore additional\n", fecui);
				break;
			}

			/*
			 * Can only handle one MAC, the first outermost mac.
			 */
			ecm_db_iface_ethernet_address_get(ii, from_nss_iface_address);
			DEBUG_TRACE("%p: Ethernet - mac: %pM\n", fecui, from_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_PPPOE:
			/*
			 * More than one PPPoE in the list is not valid!
			 */
			if (interface_type_counts[ii_type] != 0) {
				DEBUG_TRACE("%p: PPPoE - additional unsupported\n", fecui);
				rule_invalid = true;
				break;
			}

			/*
			 * Copy pppoe session info to the creation structure.
			 */
			ecm_db_iface_pppoe_session_info_get(ii, &pppoe_info);

			nircm->pppoe_rule.flow_pppoe_session_id = pppoe_info.pppoe_session_id;
			memcpy(nircm->pppoe_rule.flow_pppoe_remote_mac, pppoe_info.remote_mac, ETH_ALEN);
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_PPPOE_VALID;

			DEBUG_TRACE("%p: PPPoE - session: %x, mac: %pM\n", fecui,
					nircm->pppoe_rule.flow_pppoe_session_id,
					nircm->pppoe_rule.flow_pppoe_remote_mac);
			break;
		case ECM_DB_IFACE_TYPE_VLAN:
			DEBUG_TRACE("%p: VLAN\n", fecui);
			if (interface_type_counts[ii_type] > 1) {
				/*
				 * Can only support two vlans
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: VLAN - additional unsupported\n", fecui);
				break;
			}
			ecm_db_iface_vlan_info_get(ii, &vlan_info);
			vlan_value = ((vlan_info.vlan_tpid << 16) | vlan_info.vlan_tag);

			/*
			 * Look up the vlan device and incorporate the vlan priority into the vlan_value
			 */

			vlan_in_dev = dev_get_by_index(&init_net, ecm_db_iface_interface_identifier_get(ii));
			if (vlan_in_dev) {
				vlan_value |= vlan_dev_get_egress_prio(vlan_in_dev, pr->return_qos_tag);
				dev_put(vlan_in_dev);
				vlan_in_dev = NULL;
			}

			/*
			 * Primary or secondary (QinQ) VLAN?
			 */
			if (interface_type_counts[ii_type] == 0) {
				nircm->vlan_primary_rule.ingress_vlan_tag = vlan_value;
			} else {
				nircm->vlan_secondary_rule.ingress_vlan_tag = vlan_value;
			}
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_VLAN_VALID;

			/*
			 * If we have not yet got an ethernet mac then take this one (very unlikely as mac should have been propagated to the slave (outer) device
			 */
			if (interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET] == 0) {
				memcpy(from_nss_iface_address, vlan_info.address, ETH_ALEN);
				interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET]++;
				DEBUG_TRACE("%p: VLAN use mac: %pM\n", fecui, from_nss_iface_address);
			}
			DEBUG_TRACE("%p: vlan tag: %x\n", fecui, vlan_value);
			break;
		case ECM_DB_IFACE_TYPE_IPSEC_TUNNEL:
			DEBUG_TRACE("%p: IPSEC\n", fecui);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Can only support one ipsec
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: IPSEC - additional unsupported\n", fecui);
				break;
			}
			nircm->conn_rule.flow_interface_num = NSS_C2C_TX_INTERFACE;
			break;
		default:
			DEBUG_TRACE("%p: Ignoring: %d (%s)\n", fecui, ii_type, ii_name);
		}

		/*
		 * Seen an interface of this type
		 */
		interface_type_counts[ii_type]++;
	}
	if (rule_invalid) {
		DEBUG_WARN("%p: from/src Rule invalid\n", fecui);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto udp_accel_bad_rule;
	}

	/*
	 * Now examine the TO / DEST heirarchy list to construct the destination part of the rule
	 */
	DEBUG_TRACE("%p: Examine to/dest heirarchy list\n", fecui);
	memset(interface_type_counts, 0, sizeof(interface_type_counts));
	rule_invalid = false;
	for (list_index = to_ifaces_first; !rule_invalid && (list_index < ECM_DB_IFACE_HEIRARCHY_MAX); list_index++) {
		struct ecm_db_iface_instance *ii;
		ecm_db_iface_type_t ii_type;
		char *ii_name;

		ii = to_ifaces[list_index];
		ii_type = ecm_db_connection_iface_type_get(ii);
		ii_name = ecm_db_interface_type_to_string(ii_type);
		DEBUG_TRACE("%p: list_index: %d, ii: %p, type: %d (%s)\n", fecui, list_index, ii, ii_type, ii_name);

		/*
		 * Extract information from this interface type if it is applicable to the rule.
		 * Conflicting information may cause accel to be unsupported.
		 */
		switch (ii_type) {
			struct ecm_db_interface_info_pppoe pppoe_info;
			struct ecm_db_interface_info_vlan vlan_info;
			uint32_t vlan_value = 0;
			struct net_device *vlan_out_dev = NULL;

		case ECM_DB_IFACE_TYPE_BRIDGE:
			DEBUG_TRACE("%p: Bridge\n", fecui);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Cannot cascade bridges
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: Bridge - ignore additional\n", fecui);
				break;
			}
			ecm_db_iface_bridge_address_get(ii, to_nss_iface_address);
			DEBUG_TRACE("%p: Bridge - mac: %pM\n", fecui, to_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_ETHERNET:
			DEBUG_TRACE("%p: Ethernet\n", fecui);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Ignore additional mac addresses, these are usually as a result of address propagation
				 * from bridges down to ports etc.
				 */
				DEBUG_TRACE("%p: Ethernet - ignore additional\n", fecui);
				break;
			}

			/*
			 * Can only handle one MAC, the first outermost mac.
			 */
			ecm_db_iface_ethernet_address_get(ii, to_nss_iface_address);
			DEBUG_TRACE("%p: Ethernet - mac: %pM\n", fecui, to_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_PPPOE:
			/*
			 * More than one PPPoE in the list is not valid!
			 */
			if (interface_type_counts[ii_type] != 0) {
				DEBUG_TRACE("%p: PPPoE - additional unsupported\n", fecui);
				rule_invalid = true;
				break;
			}

			/*
			 * Copy pppoe session info to the creation structure.
			 */
			ecm_db_iface_pppoe_session_info_get(ii, &pppoe_info);
			nircm->pppoe_rule.return_pppoe_session_id = pppoe_info.pppoe_session_id;
			memcpy(nircm->pppoe_rule.return_pppoe_remote_mac, pppoe_info.remote_mac, ETH_ALEN);
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_PPPOE_VALID;

			DEBUG_TRACE("%p: PPPoE - session: %x, mac: %pM\n", fecui,
				    nircm->pppoe_rule.return_pppoe_session_id,
				    nircm->pppoe_rule.return_pppoe_remote_mac);
			break;
		case ECM_DB_IFACE_TYPE_VLAN:
			DEBUG_TRACE("%p: VLAN\n", fecui);
			if (interface_type_counts[ii_type] > 1) {
				/*
				 * Can only support two vlans
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: VLAN - additional unsupported\n", fecui);
				break;
			}
			ecm_db_iface_vlan_info_get(ii, &vlan_info);
			vlan_value = ((vlan_info.vlan_tpid << 16) | vlan_info.vlan_tag);

			/*
			 * Look up the vlan device and incorporate the vlan priority into the vlan_value
			 */
			vlan_out_dev = dev_get_by_index(&init_net, ecm_db_iface_interface_identifier_get(ii));
			if (vlan_out_dev) {
				vlan_value |= vlan_dev_get_egress_prio(vlan_out_dev, pr->flow_qos_tag);
				dev_put(vlan_out_dev);
				vlan_out_dev = NULL;
			}

			/*
			 * Primary or secondary (QinQ) VLAN?
			 */
			if (interface_type_counts[ii_type] == 0) {
				nircm->vlan_primary_rule.egress_vlan_tag = vlan_value;
			} else {
				nircm->vlan_secondary_rule.egress_vlan_tag = vlan_value;
			}
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_VLAN_VALID;

			/*
			 * If we have not yet got an ethernet mac then take this one (very unlikely as mac should have been propagated to the slave (outer) device
			 */
			if (interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET] == 0) {
				memcpy(to_nss_iface_address, vlan_info.address, ETH_ALEN);
				interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET]++;
				DEBUG_TRACE("%p: VLAN use mac: %pM\n", fecui, to_nss_iface_address);
			}
			DEBUG_TRACE("%p: vlan tag: %x\n", fecui, vlan_value);
			break;
		case ECM_DB_IFACE_TYPE_IPSEC_TUNNEL:
			DEBUG_TRACE("%p: IPSEC\n", fecui);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Can only support one ipsec
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: IPSEC - additional unsupported\n", fecui);
				break;
			}
			nircm->conn_rule.return_interface_num = NSS_C2C_TX_INTERFACE;
			break;
		default:
			DEBUG_TRACE("%p: Ignoring: %d (%s)\n", fecui, ii_type, ii_name);
		}

		/*
		 * Seen an interface of this type
		 */
		interface_type_counts[ii_type]++;
	}
	if (rule_invalid) {
		DEBUG_WARN("%p: to/dest Rule invalid\n", fecui);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto udp_accel_bad_rule;
	}

	/*
	 * Routed or bridged?
	 */
	if (ecm_db_connection_is_routed_get(fecui->ci)) {
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_ROUTED;
	} else {
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_BRIDGE_FLOW;
	}

	/*
	 * Set up the flow and return qos tags
	 */
	nircm->qos_rule.flow_qos_tag = (uint32_t)pr->flow_qos_tag;
	nircm->qos_rule.return_qos_tag = (uint32_t)pr->return_qos_tag;
	nircm->valid_flags |= NSS_IPV6_RULE_CREATE_QOS_VALID;

	/*
	 * DSCP information?
	 */
	if (pr->process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP) {
		nircm->dscp_rule.flow_dscp = pr->flow_dscp;
		nircm->dscp_rule.return_dscp = pr->return_dscp;
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_DSCP_MARKING;
		nircm->valid_flags |= NSS_IPV6_RULE_CREATE_DSCP_MARKING_VALID;
	}

	/*
	 * Set protocol
	 */
	nircm->tuple.protocol = (int32_t)IPPROTO_UDP;

	/*
	 * The src_ip is where the connection established from
	 */
	ecm_db_connection_from_address_get(fecui->ci, src_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nircm->tuple.flow_ip, src_ip);

	/*
	 * The dest_ip is where the connection is established to
	 */
	ecm_db_connection_to_address_get(fecui->ci, dest_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nircm->tuple.return_ip, dest_ip);

	/*
	 * Same approach as above for port information
	 */
	nircm->tuple.flow_ident = ecm_db_connection_from_port_get(fecui->ci);
	nircm->tuple.return_ident = ecm_db_connection_to_port_nat_get(fecui->ci);

	/*
	 * Get mac addresses.
	 * The src_mac is the mac address of the node that established the connection.
	 * This will work whether the from_node is LAN (egress) or WAN (ingress).
	 */
	ecm_db_connection_from_node_address_get(fecui->ci, (uint8_t *)nircm->conn_rule.flow_mac);

	/*
	 * The dest_mac is more complex.  For egress it is the node address of the 'to' side of the connection.
	 * For ingress it is the node adress of the NAT'ed 'to' IP.
	 * Essentially it is the MAC of node associated with create.dest_ip and this is "to nat" side.
	 */
	ecm_db_connection_to_node_address_get(fecui->ci, (uint8_t *)nircm->conn_rule.return_mac);

	/*
	 * Get MTU information
	 */
	nircm->conn_rule.flow_mtu = (uint32_t)ecm_db_connection_from_iface_mtu_get(fecui->ci);
	nircm->conn_rule.return_mtu = (uint32_t)ecm_db_connection_to_iface_mtu_get(fecui->ci);

	/*
	 * Sync our creation command from the assigned classifiers to get specific additional creation rules.
	 * NOTE: These are called in ascending order of priority and so the last classifier (highest) shall
	 * override any preceding classifiers.
	 * This also gives the classifiers a chance to see that acceleration is being attempted.
	 */
	assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(fecui->ci, assignments);
	for (aci_index = 0; aci_index < assignment_count; ++aci_index) {
		struct ecm_classifier_instance *aci;

		aci = assignments[aci_index];
		DEBUG_TRACE("%p: sync from: %p, type: %d\n", fecui, aci, aci->type_get(aci));
		aci->sync_from_v6(aci, nircm);
	}
	ecm_db_connection_assignments_release(assignment_count, assignments);

	/*
	 * Release the interface lists
	 */
	ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
	ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);

	DEBUG_INFO("%p: UDP Accelerate connection %p\n"
			"Protocol: %d\n"
			"from_mtu: %u\n"
			"to_mtu: %u\n"
			"from_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"to_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"from_mac: %pM\n"
			"to_mac: %pM\n"
			"src_iface_num: %u\n"
			"dest_iface_num: %u\n"
			"ingress_inner_vlan_tag: %u\n"
			"egress_inner_vlan_tag: %u\n"
			"ingress_outer_vlan_tag: %u\n"
			"egress_outer_vlan_tag: %u\n"
			"rule_flags: %x\n"
			"valid_flags: %x\n"



			"return_pppoe_session_id: %u\n"
			"return_pppoe_remote_mac: %pM\n"
			"flow_pppoe_session_id: %u\n"
			"flow_pppoe_remote_mac: %pM\n"
			"flow_qos_tag: %x (%u)\n"
			"return_qos_tag: %x (%u)\n"
			"flow_dscp: %x\n"
			"return_dscp: %x\n",

			fecui,
			fecui->ci,
			nircm->tuple.protocol,
			nircm->conn_rule.flow_mtu,
			nircm->conn_rule.return_mtu,
			ECM_IP_ADDR_TO_OCTAL(src_ip), nircm->tuple.flow_ident,
			ECM_IP_ADDR_TO_OCTAL(dest_ip), nircm->tuple.return_ident,
			nircm->conn_rule.flow_mac,
			nircm->conn_rule.return_mac,
			nircm->conn_rule.flow_interface_num,
			nircm->conn_rule.return_interface_num,
			nircm->vlan_primary_rule.ingress_vlan_tag,
			nircm->vlan_primary_rule.egress_vlan_tag,
			nircm->vlan_secondary_rule.ingress_vlan_tag,
			nircm->vlan_secondary_rule.egress_vlan_tag,
			nircm->rule_flags,
			nircm->valid_flags,
			nircm->pppoe_rule.return_pppoe_session_id,
			nircm->pppoe_rule.return_pppoe_remote_mac,
			nircm->pppoe_rule.flow_pppoe_session_id,
			nircm->pppoe_rule.flow_pppoe_remote_mac,
			nircm->qos_rule.flow_qos_tag, nircm->qos_rule.flow_qos_tag,
			nircm->qos_rule.return_qos_tag, nircm->qos_rule.return_qos_tag,
			nircm->dscp_rule.flow_dscp,
			nircm->dscp_rule.return_dscp);

	/*
	 * Call the rule create function
	 */
	nss_tx_status = nss_ipv6_tx(ecm_front_end_ipv6_nss_ipv6_mgr, &nim);
	if (nss_tx_status == NSS_TX_SUCCESS) {
		spin_lock_bh(&fecui->lock);
		DEBUG_ASSERT(fecui->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecui, fecui->accel_mode);
		fecui->base.stats.driver_fail = 0;		/* Reset */
		spin_unlock_bh(&fecui->lock);
		return;
	}

	/*
	 * TX failed
	 */
	spin_lock_bh(&fecui->lock);
	DEBUG_ASSERT(fecui->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecui, fecui->accel_mode);
	fecui->base.stats.driver_fail_total++;
	fecui->base.stats.driver_fail++;
	if (fecui->base.stats.driver_fail >= fecui->base.stats.driver_fail_limit) {
		DEBUG_WARN("%p: Accel failed - driver fail limit\n", fecui);
		fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DRIVER;
	}
	spin_unlock_bh(&fecui->lock);
	return;

udp_accel_bad_rule:
	;

	/*
	 * Jump to here when rule data is bad and an offload command cannot be constructed
	 */
	DEBUG_WARN("%p: Accel failed - bad rule\n", fecui);
	spin_lock_bh(&fecui->lock);
	DEBUG_ASSERT(fecui->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecui, fecui->accel_mode);
	fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_RULE;
	spin_unlock_bh(&fecui->lock);
}

/*
 * ecm_front_end_ipv6_connection_udp_destroy_callback()
 *	Callback for handling create ack/nack calls.
 */
static void ecm_front_end_ipv6_connection_udp_destroy_callback(void *app_data, struct nss_ipv6_msg *nim)
{
	struct nss_ipv6_rule_destroy_msg *nirdm = &nim->msg.rule_destroy;
	uint32_t serial = (uint32_t)app_data;
	struct ecm_db_connection_instance *ci;
	struct ecm_front_end_connection_instance *feci;
	struct ecm_front_end_ipv6_connection_udp_instance *fecui;
	ip_addr_t flow_ip;
	ip_addr_t return_ip;

	/*
	 * Is this a response to a destroy message?
	 */
	if (nim->cm.type != NSS_IPV6_TX_DESTROY_RULE_MSG) {
		DEBUG_ERROR("%p: udp destroy callback with improper type: %d\n", nim, nim->cm.type);
		return;
	}

	/*
	 * Look up ecm connection so that we can update the status.
	 */
	ci = ecm_db_connection_serial_find_and_ref(serial);
	if (!ci) {
		DEBUG_TRACE("%p: destroy callback, connection not found, serial: %u\n", nim, serial);
		return;
	}

	/*
	 * Get the front end instance
	 */
	feci = ecm_db_connection_front_end_get_and_ref(ci);
	fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;
	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);

	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(flow_ip, nirdm->tuple.flow_ip);
	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(return_ip, nirdm->tuple.return_ip);

	/*
	 * Dump some useful trace information.
	 */
	DEBUG_TRACE("%p: decelerate response for connection: %p\n", fecui, fecui->ci);
	DEBUG_TRACE("%p: flow_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecui, ECM_IP_ADDR_TO_OCTAL(flow_ip), nirdm->tuple.flow_ident);
	DEBUG_TRACE("%p: return_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecui, ECM_IP_ADDR_TO_OCTAL(return_ip), nirdm->tuple.return_ident);
	DEBUG_TRACE("%p: protocol: %d\n", fecui, nirdm->tuple.protocol);

	spin_lock_bh(&fecui->lock);

	/*
	 * If decel is not still pending then it's possible that the NSS ended acceleration by some other reason e.g. flush
	 * In which case we cannot rely on the response we get here.
	 */
	if (fecui->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_DECEL_PENDING) {
		spin_unlock_bh(&fecui->lock);

		/*
		 * Release the connections.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	DEBUG_TRACE("%p: response: %d\n", fecui, nim->cm.response);
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DECEL;
	} else {
		fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
	}

	/*
	 * UDP acceleration ends
	 */
	ecm_front_end_ipv6_udp_accelerated_count--;	/* Protocol specific counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_udp_accelerated_count >= 0, "Bad udp accel counter\n");
	ecm_front_end_ipv6_accelerated_count--;		/* General running counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_accelerated_count >= 0, "Bad accel counter\n");

	spin_unlock_bh(&fecui->lock);

	/*
	 * Release the connections.
	 */
	feci->deref(feci);
	ecm_db_connection_deref(ci);
}

/*
 * ecm_front_end_ipv6_connection_udp_front_end_decelerate()
 *	Decelerate a connection
 */
static void ecm_front_end_ipv6_connection_udp_front_end_decelerate(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_udp_instance *fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;
	struct nss_ipv6_msg nim;
	struct nss_ipv6_rule_destroy_msg *nirdm;
	ip_addr_t src_ip;
	ip_addr_t dest_ip;
	nss_tx_status_t nss_tx_status;

	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);

	/*
	 * If decelerate is in error or already pending then ignore
	 */
	spin_lock_bh(&fecui->lock);
	if (fecui->base.stats.decelerate_pending) {
		spin_unlock_bh(&fecui->lock);
		return;
	}

	/*
	 * If acceleration is pending then we cannot decelerate right now or we will race with it
	 * Set a decelerate pending flag that will be actioned when the acceleration command is complete.
	 */
	if (fecui->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING) {
		fecui->base.stats.decelerate_pending = true;
		spin_unlock_bh(&fecui->lock);
		return;
	}

	/*
	 * Can only decelerate if accelerated
	 * NOTE: This will also deny accel when the connection is in fail condition too.
	 */
	if (fecui->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_ACCEL) {
		spin_unlock_bh(&fecui->lock);
		return;
	}

	/*
	 * Initiate deceleration
	 */
	fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL_PENDING;
	spin_unlock_bh(&fecui->lock);

	/*
	 * Prepare deceleration message
	 */
	nss_cmn_msg_init(&nim.cm, NSS_IPV6_RX_INTERFACE, NSS_IPV6_TX_DESTROY_RULE_MSG,
			sizeof(struct nss_ipv6_rule_destroy_msg),
			ecm_front_end_ipv6_connection_udp_destroy_callback,
			(void *)ecm_db_connection_serial_get(fecui->ci));

	nirdm = &nim.msg.rule_destroy;
	nirdm->tuple.protocol = (int32_t)IPPROTO_UDP;



	/*
	 * Get addressing information
	 */
	ecm_db_connection_from_address_get(fecui->ci, src_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nirdm->tuple.flow_ip, src_ip);
	ecm_db_connection_to_address_get(fecui->ci, dest_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nirdm->tuple.return_ip, dest_ip);
	nirdm->tuple.flow_ident = ecm_db_connection_from_port_get(fecui->ci);
	nirdm->tuple.return_ident = ecm_db_connection_to_port_get(fecui->ci);

	DEBUG_INFO("%p: UDP Connection %p decelerate\n"
			"src_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"dest_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n",
			fecui, fecui->ci,
			ECM_IP_ADDR_TO_OCTAL(src_ip), nirdm->tuple.flow_ident,
			ECM_IP_ADDR_TO_OCTAL(dest_ip), nirdm->tuple.return_ident);

	/*
	 * Destroy the NSS connection cache entry.
	 */
	nss_tx_status = nss_ipv6_tx(ecm_front_end_ipv6_nss_ipv6_mgr, &nim);
	if (nss_tx_status == NSS_TX_SUCCESS) {
		spin_lock_bh(&fecui->lock);
		fecui->base.stats.driver_fail = 0;		/* Reset */
		spin_unlock_bh(&fecui->lock);
		return;
	}

	/*
	 * TX failed
	 */
	spin_lock_bh(&fecui->lock);
	fecui->base.stats.driver_fail_total++;
	fecui->base.stats.driver_fail++;
	if (fecui->base.stats.driver_fail >= fecui->base.stats.driver_fail_limit) {
		DEBUG_WARN("%p: Decel failed - driver fail limit\n", fecui);
		fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DRIVER;
	}
	spin_unlock_bh(&fecui->lock);
}

/*
 * ecm_front_end_ipv6_connection_udp_front_end_accel_state_get()
 *	Get acceleration state
 */
static ecm_front_end_acceleration_mode_t ecm_front_end_ipv6_connection_udp_front_end_accel_state_get(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_udp_instance *fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;
	ecm_front_end_acceleration_mode_t state;

	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);
	spin_lock_bh(&fecui->lock);
	state = fecui->accel_mode;
	spin_unlock_bh(&fecui->lock);
	return state;
}

/*
 * ecm_front_end_ipv6_connection_udp_front_end_action_seen()
 *	Acceleration action / activity has been seen for this connection.
 *
 * NOTE: Call the action_seen() method when the NSS has demonstrated that it has offloaded some data for a connection.
 */
static void ecm_front_end_ipv6_connection_udp_front_end_action_seen(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_udp_instance *fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);
	DEBUG_INFO("%p: Action seen\n", fecui);
	spin_lock_bh(&fecui->lock);
	fecui->base.stats.no_action_seen = 0;
	spin_unlock_bh(&fecui->lock);
}

/*
 * ecm_front_end_ipv6_connection_udp_front_end_accel_ceased()
 *	NSS has indicated that acceleration has stopped.
 *
 * NOTE: This is called in response to an NSS self-initiated termination of acceleration.
 * This must NOT be called because the ECM terminated the acceleration.
 */
static void ecm_front_end_ipv6_connection_udp_front_end_accel_ceased(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_udp_instance *fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);
	DEBUG_INFO("%p: accel ceased\n", fecui);

	/*
	 * If connection is no longer accelerated by the time we get here just ignore the command
	 */
	spin_lock_bh(&fecui->lock);
	if (fecui->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_ACCEL) {
		spin_unlock_bh(&fecui->lock);
		return;
	}

	/*
	 * If the no_action_seen counter was not reset then acceleration ended without any offload action
	 */
	if (fecui->base.stats.no_action_seen) {
		fecui->base.stats.no_action_seen_total++;
	}

	/*
	 * If the no_action_seen indicates successive cessations of acceleration without any offload action occuring
	 * then we fail out this connection
	 */
	if (fecui->base.stats.no_action_seen >= fecui->base.stats.no_action_seen_limit) {
		fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_NO_ACTION;
	} else {
		fecui->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
	}
	spin_unlock_bh(&fecui->lock);

	/*
	 * UDP acceleration ends
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_udp_accelerated_count--;	/* Protocol specific counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_udp_accelerated_count >= 0, "Bad udp accel counter\n");
	ecm_front_end_ipv6_accelerated_count--;		/* General running counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_accelerated_count >= 0, "Bad accel counter\n");
	spin_unlock_bh(&ecm_front_end_ipv6_lock);
}

/*
 * ecm_front_end_ipv6_connection_udp_front_end_ref()
 *	Ref a connection front end instance
 */
static void ecm_front_end_ipv6_connection_udp_front_end_ref(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_udp_instance *fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);
	spin_lock_bh(&fecui->lock);
	fecui->refs++;
	DEBUG_TRACE("%p: fecui ref %d\n", fecui, fecui->refs);
	DEBUG_ASSERT(fecui->refs > 0, "%p: ref wrap\n", fecui);
	spin_unlock_bh(&fecui->lock);
}

/*
 * ecm_front_end_ipv6_connection_udp_front_end_deref()
 *	Deref a connection front end instance
 */
static int ecm_front_end_ipv6_connection_udp_front_end_deref(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_udp_instance *fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);

	spin_lock_bh(&fecui->lock);
	fecui->refs--;
	DEBUG_ASSERT(fecui->refs >= 0, "%p: ref wrap\n", fecui);

	if (fecui->refs > 0) {
		int refs = fecui->refs;
		spin_unlock_bh(&fecui->lock);
		DEBUG_TRACE("%p: fecui deref %d\n", fecui, refs);
		return refs;
	}
	spin_unlock_bh(&fecui->lock);

	/*
	 * We can now destroy the instance
	 */
	DEBUG_TRACE("%p: fecui final\n", fecui);
	DEBUG_CLEAR_MAGIC(fecui);
	kfree(fecui);

	return 0;
}

/*
 * ecm_front_end_ipv6_connection_udp_front_end_xml_state_get()
 *	Return an XML element containing the state of this UDP front end instance
 */
static int ecm_front_end_ipv6_connection_udp_front_end_xml_state_get(struct ecm_front_end_connection_instance *feci, char *buf, int buf_sz)
{
	bool can_accel;
	ecm_front_end_acceleration_mode_t accel_mode;
	struct ecm_front_end_connection_mode_stats stats;
	struct ecm_front_end_ipv6_connection_udp_instance *fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)feci;

	DEBUG_CHECK_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC, "%p: magic failed", fecui);

	spin_lock_bh(&fecui->lock);
	can_accel = fecui->can_accel;
	accel_mode = fecui->accel_mode;
	memcpy(&stats, &feci->stats, sizeof(struct ecm_front_end_connection_mode_stats));
	spin_unlock_bh(&fecui->lock);

	return snprintf(buf, buf_sz, "<front_end_tcp can_accel=\"%d\" accel_mode=\"%d\" decelerate_pending=\"%d\""
			" no_action_seen_total=\"%d\" no_action_seen=\"%d\" no_action_seen_limit=\"%d\""
			" driver_fail_total=\"%d\" driver_fail=\"%d\" driver_fail_limit=\"%d\""
			" nss_nack_total=\"%d\" nss_nack=\"%d\" nss_nack_limit=\"%d\"/>\n",
			can_accel,
			accel_mode,
			stats.decelerate_pending,
			stats.no_action_seen_total,
			stats.no_action_seen,
			stats.no_action_seen_limit,
			stats.driver_fail_total,
			stats.driver_fail,
			stats.driver_fail_limit,
			stats.nss_nack_total,
			stats.nss_nack,
			stats.nss_nack_limit);
}

/*
 * ecm_front_end_ipv6_connection_udp_instance_alloc()
 *	Create a front end instance specific for UDP connection
 */
static struct ecm_front_end_ipv6_connection_udp_instance *ecm_front_end_ipv6_connection_udp_instance_alloc(
								struct ecm_db_connection_instance *ci,
								struct ecm_db_mapping_instance *src_mi,
								struct ecm_db_mapping_instance *dest_mi,
								bool can_accel)
{
	struct ecm_front_end_ipv6_connection_udp_instance *fecui;

	fecui = (struct ecm_front_end_ipv6_connection_udp_instance *)kzalloc(sizeof(struct ecm_front_end_ipv6_connection_udp_instance), GFP_ATOMIC | __GFP_NOWARN);
	if (!fecui) {
		DEBUG_WARN("UDP Front end alloc failed\n");
		return NULL;
	}

	/*
	 * Refs is 1 for the creator of the connection
	 */
	fecui->refs = 1;
	DEBUG_SET_MAGIC(fecui, ECM_FRONT_END_IPV6_CONNECTION_UDP_INSTANCE_MAGIC);
	spin_lock_init(&fecui->lock);

	fecui->can_accel = can_accel;
	fecui->accel_mode = (can_accel) ? ECM_FRONT_END_ACCELERATION_MODE_DECEL : ECM_FRONT_END_ACCELERATION_MODE_FAIL_DENIED;
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	fecui->base.stats.no_action_seen_limit = ecm_front_end_ipv6_no_action_limit_default;
	fecui->base.stats.driver_fail_limit = ecm_front_end_ipv6_driver_fail_limit_default;
	fecui->base.stats.nss_nack_limit = ecm_front_end_ipv6_nack_limit_default;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Copy reference to connection - no need to ref ci as ci maintains a ref to this instance instead (this instance persists for as long as ci does)
	 */
	fecui->ci = ci;

	/*
	 * Populate the methods and callbacks
	 */
	fecui->base.ref = ecm_front_end_ipv6_connection_udp_front_end_ref;
	fecui->base.deref = ecm_front_end_ipv6_connection_udp_front_end_deref;
	fecui->base.decelerate = ecm_front_end_ipv6_connection_udp_front_end_decelerate;
	fecui->base.accel_state_get = ecm_front_end_ipv6_connection_udp_front_end_accel_state_get;
	fecui->base.action_seen = ecm_front_end_ipv6_connection_udp_front_end_action_seen;
	fecui->base.accel_ceased = ecm_front_end_ipv6_connection_udp_front_end_accel_ceased;
	fecui->base.xml_state_get = ecm_front_end_ipv6_connection_udp_front_end_xml_state_get;

	return fecui;
}

/*
 * ecm_front_end_ipv6_connection_non_ported_callback()
 *	Callback for handling create ack/nack calls.
 *
 * TODO: Since we don't want to hold data structure reference across multiple heterogenous
 * CPUs, we should use the app_data as the sequence # and do a fast look up rather than
 * a 5-tuple lookup.
 */
static void ecm_front_end_ipv6_connection_non_ported_callback(void *app_data, struct nss_ipv6_msg *nim)
{
	struct nss_ipv6_rule_create_msg *nircm = &nim->msg.rule_create;
	uint32_t serial = (uint32_t)app_data;
	struct ecm_db_connection_instance *ci;
	struct ecm_front_end_connection_instance *feci;
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi;
	ip_addr_t flow_ip;
	ip_addr_t return_ip;

	/*
	 * Is this a response to a create message?
	 */
	if (nim->cm.type != NSS_IPV6_TX_CREATE_RULE_MSG) {
		DEBUG_ERROR("%p: non_ported create callback with improper type: %d, serial: %u\n", nim, nim->cm.type, serial);
		return;
	}

	/*
	 * Look up ecm connection so that we can update the status.
	 */
	ci = ecm_db_connection_serial_find_and_ref(serial);
	if (!ci) {
		DEBUG_TRACE("%p: create callback, connection not found, serial: %u\n", nim, serial);
		return;
	}

	/*
	 * Get the front end instance
	 */
	feci = ecm_db_connection_front_end_get_and_ref(ci);
	fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;
	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);

	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(flow_ip, nircm->tuple.flow_ip);
	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(return_ip, nircm->tuple.return_ip);

	/*
	 * Dump some useful trace information.
	 */
	DEBUG_TRACE("%p: accelerate response for connection: %p, serial: %u\n", fecnpi, fecnpi->ci, serial);
	DEBUG_TRACE("%p: rule_flags: %x, valid_flags: %x\n", fecnpi, nircm->rule_flags, nircm->valid_flags);
	DEBUG_TRACE("%p: flow_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecnpi, ECM_IP_ADDR_TO_OCTAL(flow_ip), nircm->tuple.flow_ident);
	DEBUG_TRACE("%p: return_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecnpi, ECM_IP_ADDR_TO_OCTAL(return_ip), nircm->tuple.return_ident);
	DEBUG_TRACE("%p: protocol: %d\n", fecnpi, nircm->tuple.protocol);

	/*
	 * Handle the creation result code.
	 */
	DEBUG_TRACE("%p: response: %d\n", fecnpi, nim->cm.response);
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		/*
		 * Creation command failed (specific reason ignored).
		 */
		DEBUG_TRACE("%p: accel nack: %d\n", fecnpi, nim->cm.error);
		spin_lock_bh(&fecnpi->lock);
		DEBUG_ASSERT(fecnpi->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Unexpected mode: %d\n", ci, fecnpi->accel_mode);
		fecnpi->base.stats.nss_nack++;
		fecnpi->base.stats.nss_nack_total++;
		if (fecnpi->base.stats.nss_nack >= fecnpi->base.stats.nss_nack_limit) {
			/*
			 * Too many NSS rejections
			 */
			fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_NSS;
		} else {
			/*
			 * Revert to decelerated
			 */
			fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
		}

		/*
		 * Clear any decelerate pending flag since we aren't accelerated anyway we can just clear this whether it is set or not
		 */
		fecnpi->base.stats.decelerate_pending = false;
		spin_unlock_bh(&fecnpi->lock);

		/*
		 * Release the connection.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	/*
	 * Create succeeded, declare that we are accelerated.
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_non_ported_accelerated_count++;	/* Protocol specific counter */
	ecm_front_end_ipv6_accelerated_count++;			/* General running counter */
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	spin_lock_bh(&fecnpi->lock);
	DEBUG_ASSERT(fecnpi->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Unexpected mode: %d\n", ci, fecnpi->accel_mode);
	fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_ACCEL;

	/*
	 * Increement the no-action counter, this is reset if offload action is seen
	 */
	fecnpi->base.stats.no_action_seen++;

	/*
	 * Clear any nack count
	 */
	fecnpi->base.stats.nss_nack = 0;

	/*
	 * Decelerate may have been attempted while we were accel pending.
	 * If decelerate is pending then we need to begin deceleration :-(
	 */
	if (!fecnpi->base.stats.decelerate_pending) {
		spin_unlock_bh(&fecnpi->lock);

		/*
		 * Release the connection.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	DEBUG_INFO("%p: Decelerate was pending\n", ci);
	fecnpi->base.stats.decelerate_pending = false;
	spin_unlock_bh(&fecnpi->lock);

	feci->decelerate(feci);

	/*
	 * Release the connection.
	 */
	feci->deref(feci);
	ecm_db_connection_deref(ci);
}

/*
 * ecm_front_end_ipv6_connection_non_ported_front_end_accelerate()
 *	Accelerate a connection
 *
 * GGG TODO Refactor this function into a single function that np, udp and tcp
 * can all use and reduce the amount of code!
 */
static void ecm_front_end_ipv6_connection_non_ported_front_end_accelerate(struct ecm_front_end_connection_instance *feci,
									struct ecm_classifier_process_response *pr)
{
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;
	int protocol;
	int32_t from_ifaces_first;
	int32_t to_ifaces_first;
	struct ecm_db_iface_instance *from_ifaces[ECM_DB_IFACE_HEIRARCHY_MAX];
	struct ecm_db_iface_instance *to_ifaces[ECM_DB_IFACE_HEIRARCHY_MAX];
	struct ecm_db_iface_instance *from_nss_iface;
	struct ecm_db_iface_instance *to_nss_iface;
	int32_t from_nss_iface_id;
	int32_t to_nss_iface_id;
	uint8_t from_nss_iface_address[ETH_ALEN];
	uint8_t to_nss_iface_address[ETH_ALEN];
	struct nss_ipv6_msg nim;
	struct nss_ipv6_rule_create_msg *nircm;
	struct ecm_classifier_instance *assignments[ECM_CLASSIFIER_TYPES];
	int aci_index;
	int assignment_count;
	nss_tx_status_t nss_tx_status;
	int32_t list_index;
	int32_t interface_type_counts[ECM_DB_IFACE_TYPE_COUNT];
	bool rule_invalid;
	ip_addr_t src_ip;
	ip_addr_t dest_ip;

	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);

	/*
	 * Can this connection be accelerated at all?
	 */
	DEBUG_INFO("%p: Accel conn: %p\n", fecnpi, fecnpi->ci);
	spin_lock_bh(&fecnpi->lock);
	if (fecnpi->accel_mode <= ECM_FRONT_END_ACCELERATION_MODE_FAIL_DENIED) {
		spin_unlock_bh(&fecnpi->lock);
		DEBUG_TRACE("%p: accel %p denied\n", fecnpi, fecnpi->ci);
		return;
	}

	/*
	 * For non-ported protocols we only support IPIP
	 */
	protocol = ecm_db_connection_protocol_get(fecnpi->ci);
	if ((protocol != IPPROTO_IPIP)) {
		fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_RULE;
		spin_unlock_bh(&fecnpi->lock);
		DEBUG_TRACE("%p: unsupported protocol: %d\n", fecnpi, protocol);
		return;
	}

	/*
	 * For non-ported protocols we only support IPv6 in 4 or ESP
	 */
	protocol = ecm_db_connection_protocol_get(fecnpi->ci);
	if ((protocol != IPPROTO_IPIP) && (protocol != IPPROTO_ESP)) {
		fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_RULE;
		spin_unlock_bh(&fecnpi->lock);
		DEBUG_TRACE("%p: unsupported protocol: %d\n", fecnpi, protocol);
		return;
	}

	/*
	 * If acceleration mode is anything other than "not accelerated" then ignore.
	 */
	if (fecnpi->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_DECEL) {
		spin_unlock_bh(&fecnpi->lock);
		DEBUG_TRACE("%p: Ignoring wrong mode accel for conn: %p\n", fecnpi, fecnpi->ci);
		return;
	}
	fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING;
	spin_unlock_bh(&fecnpi->lock);

	/*
	 * Okay construct an accel command.
	 * Initialise creation structure.
	 * NOTE: We leverage the app_data void pointer to be our 32 bit connection serial number.
	 * When we get it back we re-cast it to a uint32 and do a faster connection lookup.
	 */
	memset(&nim, 0, sizeof(struct nss_ipv6_msg));
	nss_cmn_msg_init(&nim.cm, NSS_IPV6_RX_INTERFACE, NSS_IPV6_TX_CREATE_RULE_MSG,
			sizeof(struct nss_ipv6_rule_create_msg),
			ecm_front_end_ipv6_connection_non_ported_callback,
			(void *)ecm_db_connection_serial_get(fecnpi->ci));

	nircm = &nim.msg.rule_create;
	nircm->valid_flags = 0;
	nircm->rule_flags = 0;

	/*
	 * Initialize VLAN tag information
	 */
	nircm->vlan_primary_rule.ingress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_primary_rule.egress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_secondary_rule.ingress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;
	nircm->vlan_secondary_rule.egress_vlan_tag = ECM_NSS_CONNMGR_VLAN_ID_NOT_CONFIGURED;

	/*
	 * Get the interface lists of the connection, we must have at least one interface in the list to continue
	 */
	from_ifaces_first = ecm_db_connection_from_interfaces_get_and_ref(fecnpi->ci, from_ifaces);
	if (from_ifaces_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
		DEBUG_WARN("%p: Accel attempt failed - no interfaces in from_interfaces list!\n", fecnpi);
		goto non_ported_accel_bad_rule;
	}

	to_ifaces_first = ecm_db_connection_to_interfaces_get_and_ref(fecnpi->ci, to_ifaces);
	if (to_ifaces_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
		DEBUG_WARN("%p: Accel attempt failed - no interfaces in to_interfaces list!\n", fecnpi);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		goto non_ported_accel_bad_rule;
	}

	/*
	 * First interface in each must be a known nss interface
	 */
	from_nss_iface = from_ifaces[from_ifaces_first];
	to_nss_iface = to_ifaces[to_ifaces_first];
	from_nss_iface_id = ecm_db_iface_nss_interface_identifier_get(from_nss_iface);
	to_nss_iface_id = ecm_db_iface_nss_interface_identifier_get(to_nss_iface);
	if ((from_nss_iface_id < 0) || (to_nss_iface_id < 0)) {
		DEBUG_TRACE("%p: from_nss_iface_id: %d, to_nss_iface_id: %d\n", fecnpi, from_nss_iface_id, to_nss_iface_id);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto non_ported_accel_bad_rule;
	}

	/*
	 * New rule being created
	 */
	nircm->valid_flags |= NSS_IPV6_RULE_CREATE_CONN_VALID;

	/*
	 * Set interface numbers involved in accelerating this connection.
	 * These are the outer facing addresses from the heirarchy interface lists we got above.
	 * These may be overridden later if we detect special interface types e.g. ipsec.
	 */
	nircm->conn_rule.flow_interface_num = from_nss_iface_id;
	nircm->conn_rule.return_interface_num = to_nss_iface_id;

	/*
	 * We know that each outward facing interface is known to the NSS and so this connection could be accelerated.
	 * However the lists may also specify other interesting details that must be included in the creation command,
	 * for example, ethernet MAC, VLAN tagging or PPPoE session information.
	 * We get this information by walking from the outer to the innermost interface for each list and examine the interface types.
	 *
	 * Start with the 'from' (src) side.
	 * NOTE: The lists may contain a complex heirarchy of similar type of interface e.g. multiple vlans or tunnels within tunnels.
	 * This NSS cannot handle that - there is no way to describe this in the rule - if we see multiple types that would conflict we have to abort.
	 */
	DEBUG_TRACE("%p: Examine from/src heirarchy list\n", fecnpi);
	memset(interface_type_counts, 0, sizeof(interface_type_counts));
	rule_invalid = false;
	for (list_index = from_ifaces_first; !rule_invalid && (list_index < ECM_DB_IFACE_HEIRARCHY_MAX); list_index++) {
		struct ecm_db_iface_instance *ii;
		ecm_db_iface_type_t ii_type;
		char *ii_name;

		ii = from_ifaces[list_index];
		ii_type = ecm_db_connection_iface_type_get(ii);
		ii_name = ecm_db_interface_type_to_string(ii_type);
		DEBUG_TRACE("%p: list_index: %d, ii: %p, type: %d (%s)\n", fecnpi, list_index, ii, ii_type, ii_name);

		/*
		 * Extract information from this interface type if it is applicable to the rule.
		 * Conflicting information may cause accel to be unsupported.
		 */
		switch (ii_type) {
			struct ecm_db_interface_info_pppoe pppoe_info;
			struct ecm_db_interface_info_vlan vlan_info;
			uint32_t vlan_value = 0;
			struct net_device *vlan_in_dev = NULL;

		case ECM_DB_IFACE_TYPE_BRIDGE:
			DEBUG_TRACE("%p: Bridge\n", fecnpi);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Cannot cascade bridges
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: Bridge - ignore additional\n", fecnpi);
				break;
			}
			ecm_db_iface_bridge_address_get(ii, from_nss_iface_address);
			DEBUG_TRACE("%p: Bridge - mac: %pM\n", fecnpi, from_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_ETHERNET:
			DEBUG_TRACE("%p: Ethernet\n", fecnpi);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Ignore additional mac addresses, these are usually as a result of address propagation
				 * from bridges down to ports etc.
				 */
				DEBUG_TRACE("%p: Ethernet - ignore additional\n", fecnpi);
				break;
			}

			/*
			 * Can only handle one MAC, the first outermost mac.
			 */
			ecm_db_iface_ethernet_address_get(ii, from_nss_iface_address);
			DEBUG_TRACE("%p: Ethernet - mac: %pM\n", fecnpi, from_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_PPPOE:
			/*
			 * More than one PPPoE in the list is not valid!
			 */
			if (interface_type_counts[ii_type] != 0) {
				DEBUG_TRACE("%p: PPPoE - additional unsupported\n", fecnpi);
				rule_invalid = true;
				break;
			}

			/*
			 * Copy pppoe session info to the creation structure.
			 */
			ecm_db_iface_pppoe_session_info_get(ii, &pppoe_info);

			nircm->pppoe_rule.flow_pppoe_session_id = pppoe_info.pppoe_session_id;
			memcpy(nircm->pppoe_rule.flow_pppoe_remote_mac, pppoe_info.remote_mac, ETH_ALEN);
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_PPPOE_VALID;

			DEBUG_TRACE("%p: PPPoE - session: %x, mac: %pM\n", fecnpi,
					nircm->pppoe_rule.flow_pppoe_session_id,
					nircm->pppoe_rule.flow_pppoe_remote_mac);
			break;
		case ECM_DB_IFACE_TYPE_VLAN:
			DEBUG_TRACE("%p: VLAN\n", fecnpi);
			if (interface_type_counts[ii_type] > 1) {
				/*
				 * Can only support two vlans
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: VLAN - additional unsupported\n", fecnpi);
				break;
			}
			ecm_db_iface_vlan_info_get(ii, &vlan_info);
			vlan_value = ((vlan_info.vlan_tpid << 16) | vlan_info.vlan_tag);

			/*
			 * Look up the vlan device and incorporate the vlan priority into the vlan_value
			 */
			vlan_in_dev = dev_get_by_index(&init_net, ecm_db_iface_interface_identifier_get(ii));
			if (vlan_in_dev) {
				vlan_value |= vlan_dev_get_egress_prio(vlan_in_dev, pr->return_qos_tag);
				dev_put(vlan_in_dev);
				vlan_in_dev = NULL;
			}

			/*
			 * Primary or secondary (QinQ) VLAN?
			 */
			if (interface_type_counts[ii_type] == 0) {
				nircm->vlan_primary_rule.ingress_vlan_tag = vlan_value;
			} else {
				nircm->vlan_secondary_rule.ingress_vlan_tag = vlan_value;
			}
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_VLAN_VALID;

			/*
			 * If we have not yet got an ethernet mac then take this one (very unlikely as mac should have been propagated to the slave (outer) device
			 */
			if (interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET] == 0) {
				memcpy(from_nss_iface_address, vlan_info.address, ETH_ALEN);
				interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET]++;
				DEBUG_TRACE("%p: VLAN use mac: %pM\n", fecnpi, from_nss_iface_address);
			}
			DEBUG_TRACE("%p: vlan tag: %x\n", fecnpi, vlan_value);
			break;
		case ECM_DB_IFACE_TYPE_IPSEC_TUNNEL:
			DEBUG_TRACE("%p: IPSEC\n", fecnpi);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Can only support one ipsec
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: IPSEC - additional unsupported\n", fecnpi);
				break;
			}
			nircm->conn_rule.flow_interface_num = NSS_C2C_TX_INTERFACE;
			break;
		default:
			DEBUG_TRACE("%p: Ignoring: %d (%s)\n", fecnpi, ii_type, ii_name);
		}

		/*
		 * Seen an interface of this type
		 */
		interface_type_counts[ii_type]++;
	}
	if (rule_invalid) {
		DEBUG_WARN("%p: from/src Rule invalid\n", fecnpi);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto non_ported_accel_bad_rule;
	}

	/*
	 * Now examine the TO / DEST heirarchy list to construct the destination part of the rule
	 */
	DEBUG_TRACE("%p: Examine to/dest heirarchy list\n", fecnpi);
	memset(interface_type_counts, 0, sizeof(interface_type_counts));
	rule_invalid = false;
	for (list_index = to_ifaces_first; !rule_invalid && (list_index < ECM_DB_IFACE_HEIRARCHY_MAX); list_index++) {
		struct ecm_db_iface_instance *ii;
		ecm_db_iface_type_t ii_type;
		char *ii_name;

		ii = to_ifaces[list_index];
		ii_type = ecm_db_connection_iface_type_get(ii);
		ii_name = ecm_db_interface_type_to_string(ii_type);
		DEBUG_TRACE("%p: list_index: %d, ii: %p, type: %d (%s)\n", fecnpi, list_index, ii, ii_type, ii_name);

		/*
		 * Extract information from this interface type if it is applicable to the rule.
		 * Conflicting information may cause accel to be unsupported.
		 */
		switch (ii_type) {
			struct ecm_db_interface_info_pppoe pppoe_info;
			struct ecm_db_interface_info_vlan vlan_info;
			uint32_t vlan_value = 0;
			struct net_device *vlan_out_dev = NULL;

		case ECM_DB_IFACE_TYPE_BRIDGE:
			DEBUG_TRACE("%p: Bridge\n", fecnpi);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Cannot cascade bridges
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: Bridge - ignore additional\n", fecnpi);
				break;
			}
			ecm_db_iface_bridge_address_get(ii, to_nss_iface_address);
			DEBUG_TRACE("%p: Bridge - mac: %pM\n", fecnpi, to_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_ETHERNET:
			DEBUG_TRACE("%p: Ethernet\n", fecnpi);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Ignore additional mac addresses, these are usually as a result of address propagation
				 * from bridges down to ports etc.
				 */
				DEBUG_TRACE("%p: Ethernet - ignore additional\n", fecnpi);
				break;
			}

			/*
			 * Can only handle one MAC, the first outermost mac.
			 */
			ecm_db_iface_ethernet_address_get(ii, to_nss_iface_address);
			DEBUG_TRACE("%p: Ethernet - mac: %pM\n", fecnpi, to_nss_iface_address);
			break;
		case ECM_DB_IFACE_TYPE_PPPOE:
			/*
			 * More than one PPPoE in the list is not valid!
			 */
			if (interface_type_counts[ii_type] != 0) {
				DEBUG_TRACE("%p: PPPoE - additional unsupported\n", fecnpi);
				rule_invalid = true;
				break;
			}

			/*
			 * Copy pppoe session info to the creation structure.
			 */
			ecm_db_iface_pppoe_session_info_get(ii, &pppoe_info);
			nircm->pppoe_rule.return_pppoe_session_id = pppoe_info.pppoe_session_id;
			memcpy(nircm->pppoe_rule.return_pppoe_remote_mac, pppoe_info.remote_mac, ETH_ALEN);
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_PPPOE_VALID;

			DEBUG_TRACE("%p: PPPoE - session: %x, mac: %pM\n", fecnpi,
				    nircm->pppoe_rule.return_pppoe_session_id,
				    nircm->pppoe_rule.return_pppoe_remote_mac);
			break;
		case ECM_DB_IFACE_TYPE_VLAN:
			DEBUG_TRACE("%p: VLAN\n", fecnpi);
			if (interface_type_counts[ii_type] > 1) {
				/*
				 * Can only support two vlans
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: VLAN - additional unsupported\n", fecnpi);
				break;
			}
			ecm_db_iface_vlan_info_get(ii, &vlan_info);
			vlan_value = ((vlan_info.vlan_tpid << 16) | vlan_info.vlan_tag);

			/*
			 * Look up the vlan device and incorporate the vlan priority into the vlan_value
			 */
			vlan_out_dev = dev_get_by_index(&init_net, ecm_db_iface_interface_identifier_get(ii));
			if (vlan_out_dev) {
				vlan_value |= vlan_dev_get_egress_prio(vlan_out_dev, pr->flow_qos_tag);
				dev_put(vlan_out_dev);
				vlan_out_dev = NULL;
			}

			/*
			 * Primary or secondary (QinQ) VLAN?
			 */
			if (interface_type_counts[ii_type] == 0) {
				nircm->vlan_primary_rule.egress_vlan_tag = vlan_value;
			} else {
				nircm->vlan_secondary_rule.egress_vlan_tag = vlan_value;
			}
			nircm->valid_flags |= NSS_IPV6_RULE_CREATE_VLAN_VALID;

			/*
			 * If we have not yet got an ethernet mac then take this one (very unlikely as mac should have been propagated to the slave (outer) device
			 */
			if (interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET] == 0) {
				memcpy(to_nss_iface_address, vlan_info.address, ETH_ALEN);
				interface_type_counts[ECM_DB_IFACE_TYPE_ETHERNET]++;
				DEBUG_TRACE("%p: VLAN use mac: %pM\n", fecnpi, to_nss_iface_address);
			}
			DEBUG_TRACE("%p: vlan tag: %x\n", fecnpi, vlan_value);
			break;
		case ECM_DB_IFACE_TYPE_IPSEC_TUNNEL:
			DEBUG_TRACE("%p: IPSEC\n", fecnpi);
			if (interface_type_counts[ii_type] != 0) {
				/*
				 * Can only support one ipsec
				 */
				rule_invalid = true;
				DEBUG_TRACE("%p: IPSEC - additional unsupported\n", fecnpi);
				break;
			}
			nircm->conn_rule.return_interface_num = NSS_C2C_TX_INTERFACE;
			break;
		default:
			DEBUG_TRACE("%p: Ignoring: %d (%s)\n", fecnpi, ii_type, ii_name);
		}

		/*
		 * Seen an interface of this type
		 */
		interface_type_counts[ii_type]++;
	}
	if (rule_invalid) {
		DEBUG_WARN("%p: to/dest Rule invalid\n", fecnpi);
		ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
		ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);
		goto non_ported_accel_bad_rule;
	}

	/*
	 * Routed or bridged?
	 */
	if (ecm_db_connection_is_routed_get(fecnpi->ci)) {
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_ROUTED;
	} else {
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_BRIDGE_FLOW;
	}

	/*
	 * Set up the flow and return qos tags
	 */
	nircm->qos_rule.flow_qos_tag = (uint32_t)pr->flow_qos_tag;
	nircm->qos_rule.return_qos_tag = (uint32_t)pr->return_qos_tag;
	nircm->valid_flags |= NSS_IPV6_RULE_CREATE_QOS_VALID;

	/*
	 * DSCP information?
	 */
	if (pr->process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP) {
		nircm->dscp_rule.flow_dscp = pr->flow_dscp;
		nircm->dscp_rule.return_dscp = pr->return_dscp;
		nircm->rule_flags |= NSS_IPV6_RULE_CREATE_FLAG_DSCP_MARKING;
		nircm->valid_flags |= NSS_IPV6_RULE_CREATE_DSCP_MARKING_VALID;
	}

	/*
	 * Set protocol
	 */
	nircm->tuple.protocol = (int32_t)protocol;

	/*
	 * The flow_ip is where the connection established from
	 */
	ecm_db_connection_from_address_get(fecnpi->ci, src_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nircm->tuple.flow_ip, src_ip);

	/*
	 * The return_ip is where the connection is established to
	 */
	ecm_db_connection_to_address_get(fecnpi->ci, dest_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nircm->tuple.return_ip, dest_ip);

	/*
	 * Same approach as above for port information
	 */
	nircm->tuple.flow_ident = ecm_db_connection_from_port_get(fecnpi->ci);
	nircm->tuple.return_ident = ecm_db_connection_to_port_nat_get(fecnpi->ci);

	/*
	 * Get mac addresses.
	 * The src_mac is the mac address of the node that established the connection.
	 * This will work whether the from_node is LAN (egress) or WAN (ingress).
	 */
	ecm_db_connection_from_node_address_get(fecnpi->ci, (uint8_t *)nircm->conn_rule.flow_mac);

	/*
	 * The dest_mac is the mac address of the node that the connection is esatblished to.
	 */
	ecm_db_connection_to_nat_node_address_get(fecnpi->ci, (uint8_t *)nircm->conn_rule.return_mac);

	/*
	 * Get MTU information
	 */
	nircm->conn_rule.flow_mtu = (uint32_t)ecm_db_connection_from_iface_mtu_get(fecnpi->ci);
	nircm->conn_rule.return_mtu = (uint32_t)ecm_db_connection_to_iface_mtu_get(fecnpi->ci);

	/*
	 * Sync our creation command from the assigned classifiers to get specific additional creation rules.
	 * NOTE: These are called in ascending order of priority and so the last classifier (highest) shall
	 * override any preceding classifiers.
	 * This also gives the classifiers a chance to see that acceleration is being attempted.
	 */
	assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(fecnpi->ci, assignments);
	for (aci_index = 0; aci_index < assignment_count; ++aci_index) {
		struct ecm_classifier_instance *aci;

		aci = assignments[aci_index];
		DEBUG_TRACE("%p: sync from: %p, type: %d\n", fecnpi, aci, aci->type_get(aci));
		aci->sync_from_v6(aci, nircm);
	}
	ecm_db_connection_assignments_release(assignment_count, assignments);

	/*
	 * Release the interface lists
	 */
	ecm_db_connection_interfaces_deref(from_ifaces, from_ifaces_first);
	ecm_db_connection_interfaces_deref(to_ifaces, to_ifaces_first);

	DEBUG_INFO("%p: NON_PORTED Accelerate connection %p\n"
			"Protocol: %d\n"
			"from_mtu: %u\n"
			"to_mtu: %u\n"
			"from_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"to_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"from_mac: %pM\n"
			"to_mac: %pM\n"
			"src_iface_num: %u\n"
			"dest_iface_num: %u\n"
			"ingress_inner_vlan_tag: %u\n"
			"egress_inner_vlan_tag: %u\n"
			"ingress_outer_vlan_tag: %u\n"
			"egress_outer_vlan_tag: %u\n"
			"rule_flags: %x\n"
			"valid_flags: %x\n"
			"return_pppoe_session_id: %u\n"
			"return_pppoe_remote_mac: %pM\n"
			"flow_pppoe_session_id: %u\n"
			"flow_pppoe_remote_mac: %pM\n"
			"flow_qos_tag: %x (%u)\n"
			"return_qos_tag: %x (%u)\n"
			"flow_dscp: %x\n"
			"return_dscp: %x\n",
			fecnpi,
			fecnpi->ci,
			nircm->tuple.protocol,
			nircm->conn_rule.flow_mtu,
			nircm->conn_rule.return_mtu,
			ECM_IP_ADDR_TO_OCTAL(src_ip), nircm->tuple.flow_ident,
			ECM_IP_ADDR_TO_OCTAL(dest_ip), nircm->tuple.return_ident,
			nircm->conn_rule.flow_mac,
			nircm->conn_rule.return_mac,
			nircm->conn_rule.flow_interface_num,
			nircm->conn_rule.return_interface_num,
			nircm->vlan_primary_rule.ingress_vlan_tag,
			nircm->vlan_primary_rule.egress_vlan_tag,
			nircm->vlan_secondary_rule.ingress_vlan_tag,
			nircm->vlan_secondary_rule.egress_vlan_tag,
			nircm->rule_flags,
			nircm->valid_flags,
			nircm->pppoe_rule.return_pppoe_session_id,
			nircm->pppoe_rule.return_pppoe_remote_mac,
			nircm->pppoe_rule.flow_pppoe_session_id,
			nircm->pppoe_rule.flow_pppoe_remote_mac,
			nircm->qos_rule.flow_qos_tag, nircm->qos_rule.flow_qos_tag,
			nircm->qos_rule.return_qos_tag, nircm->qos_rule.return_qos_tag,
			nircm->dscp_rule.flow_dscp,
			nircm->dscp_rule.return_dscp);

	/*
	 * Call the rule create function
	 */
	nss_tx_status = nss_ipv6_tx(ecm_front_end_ipv6_nss_ipv6_mgr, &nim);
	if (nss_tx_status == NSS_TX_SUCCESS) {
		spin_lock_bh(&fecnpi->lock);
		DEBUG_ASSERT(fecnpi->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecnpi, fecnpi->accel_mode);
		fecnpi->base.stats.driver_fail = 0;		/* Reset */
		spin_unlock_bh(&fecnpi->lock);
		return;
	}

	/*
	 * TX failed
	 */
	spin_lock_bh(&fecnpi->lock);
	DEBUG_ASSERT(fecnpi->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecnpi, fecnpi->accel_mode);
	fecnpi->base.stats.driver_fail_total++;
	fecnpi->base.stats.driver_fail++;
	if (fecnpi->base.stats.driver_fail >= fecnpi->base.stats.driver_fail_limit) {
		DEBUG_WARN("%p: Accel failed - driver fail limit\n", fecnpi);
		fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DRIVER;
	}
	spin_unlock_bh(&fecnpi->lock);
	return;

non_ported_accel_bad_rule:
	;

	/*
	 * Jump to here when rule data is bad and an offload command cannot be constructed
	 */
	DEBUG_WARN("%p: Accel failed - bad rule\n", fecnpi);
	spin_lock_bh(&fecnpi->lock);
	DEBUG_ASSERT(fecnpi->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING, "%p: Accel mode unexpected: %d\n", fecnpi, fecnpi->accel_mode);
	fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_RULE;
	spin_unlock_bh(&fecnpi->lock);
}

/*
 * ecm_front_end_ipv6_connection_non_ported_destroy_callback()
 *	Callback for handling create ack/nack calls.
 */
static void ecm_front_end_ipv6_connection_non_ported_destroy_callback(void *app_data, struct nss_ipv6_msg *nim)
{
	struct nss_ipv6_rule_destroy_msg *nirdm = &nim->msg.rule_destroy;
	uint32_t serial = (uint32_t)app_data;
	struct ecm_db_connection_instance *ci;
	struct ecm_front_end_connection_instance *feci;
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi;
	ip_addr_t flow_ip;
	ip_addr_t return_ip;

	/*
	 * Is this a response to a destroy message?
	 */
	if (nim->cm.type != NSS_IPV6_TX_DESTROY_RULE_MSG) {
		DEBUG_ERROR("%p: non_ported destroy callback with improper type: %d\n", nim, nim->cm.type);
		return;
	}

	/*
	 * Look up ecm connection so that we can update the status.
	 */
	ci = ecm_db_connection_serial_find_and_ref(serial);
	if (!ci) {
		DEBUG_TRACE("%p: destroy callback, connection not found, serial: %u\n", nim, serial);
		return;
	}

	/*
	 * Get the front end instance
	 */
	feci = ecm_db_connection_front_end_get_and_ref(ci);
	fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;
	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);

	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(flow_ip, nirdm->tuple.flow_ip);
	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(return_ip, nirdm->tuple.return_ip);

	/*
	 * Dump some useful trace information.
	 */
	DEBUG_TRACE("%p: decelerate response for connection: %p\n", fecnpi, fecnpi->ci);
	DEBUG_TRACE("%p: flow_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecnpi, ECM_IP_ADDR_TO_OCTAL(flow_ip), nirdm->tuple.flow_ident);
	DEBUG_TRACE("%p: return_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n", fecnpi, ECM_IP_ADDR_TO_OCTAL(return_ip), nirdm->tuple.return_ident);
	DEBUG_TRACE("%p: protocol: %d\n", fecnpi, nirdm->tuple.protocol);

	spin_lock_bh(&fecnpi->lock);

	/*
	 * If decel is not still pending then it's possible that the NSS ended acceleration by some other reason e.g. flush
	 * In which case we cannot rely on the response we get here.
	 */
	if (fecnpi->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_DECEL_PENDING) {
		spin_unlock_bh(&fecnpi->lock);

		/*
		 * Release the connections.
		 */
		feci->deref(feci);
		ecm_db_connection_deref(ci);
		return;
	}

	DEBUG_TRACE("%p: response: %d\n", fecnpi, nim->cm.response);
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DECEL;
	} else {
		fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
	}

	/*
	 * NON_PORTED acceleration ends
	 */
	ecm_front_end_ipv6_non_ported_accelerated_count--;	/* Protocol specific counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_non_ported_accelerated_count >= 0, "Bad non_ported accel counter\n");
	ecm_front_end_ipv6_accelerated_count--;		/* General running counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_accelerated_count >= 0, "Bad accel counter\n");

	spin_unlock_bh(&fecnpi->lock);

	/*
	 * Release the connections.
	 */
	feci->deref(feci);
	ecm_db_connection_deref(ci);
}

/*
 * ecm_front_end_ipv6_connection_non_ported_front_end_decelerate()
 *	Decelerate a connection
 */
static void ecm_front_end_ipv6_connection_non_ported_front_end_decelerate(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;
	struct nss_ipv6_msg nim;
	struct nss_ipv6_rule_destroy_msg *nirdm;
	ip_addr_t src_ip;
	ip_addr_t dest_ip;
	nss_tx_status_t nss_tx_status;
	int protocol;

	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);

	/*
	 * For non-ported protocols we only support IPIP.
	 */
	protocol = ecm_db_connection_protocol_get(fecnpi->ci);
	if ((protocol != IPPROTO_IPIP)) {
		DEBUG_TRACE("%p: unsupported protocol: %d\n", fecnpi, protocol);
		return;
	}

	/*
	 * If decelerate is in error or already pending then ignore
	 */
	spin_lock_bh(&fecnpi->lock);
	if (fecnpi->base.stats.decelerate_pending) {
		spin_unlock_bh(&fecnpi->lock);
		return;
	}

	/*
	 * If acceleration is pending then we cannot decelerate right now or we will race with it
	 * Set a decelerate pending flag that will be actioned when the acceleration command is complete.
	 */
	if (fecnpi->accel_mode == ECM_FRONT_END_ACCELERATION_MODE_ACCEL_PENDING) {
		fecnpi->base.stats.decelerate_pending = true;
		spin_unlock_bh(&fecnpi->lock);
		return;
	}

	/*
	 * Can only decelerate if accelerated
	 * NOTE: This will also deny accel when the connection is in fail condition too.
	 */
	if (fecnpi->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_ACCEL) {
		spin_unlock_bh(&fecnpi->lock);
		return;
	}

	/*
	 * Initiate deceleration
	 */
	fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL_PENDING;
	spin_unlock_bh(&fecnpi->lock);

	/*
	 * Prepare deceleration message
	 */
	nss_cmn_msg_init(&nim.cm, NSS_IPV6_RX_INTERFACE, NSS_IPV6_TX_DESTROY_RULE_MSG,
			sizeof(struct nss_ipv6_rule_destroy_msg),
			ecm_front_end_ipv6_connection_non_ported_destroy_callback,
			(void *)ecm_db_connection_serial_get(fecnpi->ci));

	nirdm = &nim.msg.rule_destroy;
	nirdm->tuple.protocol = (int32_t)protocol;


	/*
	 * Get addressing information
	 */
	ecm_db_connection_from_address_get(fecnpi->ci, src_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nirdm->tuple.flow_ip, src_ip);
	ecm_db_connection_to_address_nat_get(fecnpi->ci, dest_ip);
	ECM_IP_ADDR_TO_NSS_IPV6_ADDR(nirdm->tuple.return_ip, dest_ip);
	nirdm->tuple.flow_ident = ecm_db_connection_from_port_get(fecnpi->ci);
	nirdm->tuple.return_ident = ecm_db_connection_to_port_nat_get(fecnpi->ci);

	DEBUG_INFO("%p: NON_PORTED Connection %p decelerate\n"
			"src_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"dest_ip: " ECM_IP_ADDR_OCTAL_FMT ":%d\n",
			fecnpi, fecnpi->ci,
			ECM_IP_ADDR_TO_OCTAL(src_ip), nirdm->tuple.flow_ident,
			ECM_IP_ADDR_TO_OCTAL(dest_ip), nirdm->tuple.return_ident);

	/*
	 * Destroy the NSS connection cache entry.
	 */
	nss_tx_status = nss_ipv6_tx(ecm_front_end_ipv6_nss_ipv6_mgr, &nim);
	if (nss_tx_status == NSS_TX_SUCCESS) {
		spin_lock_bh(&fecnpi->lock);
		fecnpi->base.stats.driver_fail = 0;		/* Reset */
		spin_unlock_bh(&fecnpi->lock);
		return;
	}

	/*
	 * TX failed
	 */
	spin_lock_bh(&fecnpi->lock);
	fecnpi->base.stats.driver_fail_total++;
	fecnpi->base.stats.driver_fail++;
	if (fecnpi->base.stats.driver_fail >= fecnpi->base.stats.driver_fail_limit) {
		DEBUG_WARN("%p: Decel failed - driver fail limit\n", fecnpi);
		fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_DRIVER;
	}
	spin_unlock_bh(&fecnpi->lock);
}

/*
 * ecm_front_end_ipv6_connection_non_ported_front_end_accel_state_get()
 *	Get acceleration state
 */
static ecm_front_end_acceleration_mode_t ecm_front_end_ipv6_connection_non_ported_front_end_accel_state_get(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;
	ecm_front_end_acceleration_mode_t state;

	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);
	spin_lock_bh(&fecnpi->lock);
	state = fecnpi->accel_mode;
	spin_unlock_bh(&fecnpi->lock);
	return state;
}

/*
 * ecm_front_end_ipv6_connection_non_ported_front_end_action_seen()
 *	Acceleration action / activity has been seen for this connection.
 *
 * NOTE: Call the action_seen() method when the NSS has demonstrated that it has offloaded some data for a connection.
 */
static void ecm_front_end_ipv6_connection_non_ported_front_end_action_seen(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;

	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);

	DEBUG_INFO("%p: Action seen\n", fecnpi);
	spin_lock_bh(&fecnpi->lock);
	fecnpi->base.stats.no_action_seen = 0;
	spin_unlock_bh(&fecnpi->lock);
}

/*
 * ecm_front_end_ipv6_connection_non_ported_front_end_accel_ceased()
 *	NSS has indicated that acceleration has stopped.
 *
 * NOTE: This is called in response to an NSS self-initiated termination of acceleration.
 * This must NOT be called because the ECM terminated the acceleration.
 */
static void ecm_front_end_ipv6_connection_non_ported_front_end_accel_ceased(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;

	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);

	DEBUG_INFO("%p: accel ceased\n", fecnpi);

	/*
	 * If connection is no longer accelerated by the time we get here just ignore the command
	 */
	spin_lock_bh(&fecnpi->lock);
	if (fecnpi->accel_mode != ECM_FRONT_END_ACCELERATION_MODE_ACCEL) {
		spin_unlock_bh(&fecnpi->lock);
		return;
	}

	/*
	 * If the no_action_seen counter was not reset then acceleration ended without any offload action
	 */
	if (fecnpi->base.stats.no_action_seen) {
		fecnpi->base.stats.no_action_seen_total++;
	}

	/*
	 * If the no_action_seen indicates successive cessations of acceleration without any offload action occuring
	 * then we fail out this connection
	 */
	if (fecnpi->base.stats.no_action_seen >= fecnpi->base.stats.no_action_seen_limit) {
		fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_FAIL_NO_ACTION;
	} else {
		fecnpi->accel_mode = ECM_FRONT_END_ACCELERATION_MODE_DECEL;
	}
	spin_unlock_bh(&fecnpi->lock);

	/*
	 * Non-Ported acceleration ends
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_non_ported_accelerated_count--;	/* Protocol specific counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_non_ported_accelerated_count >= 0, "Bad non-ported accel counter\n");
	ecm_front_end_ipv6_accelerated_count--;		/* General running counter */
	DEBUG_ASSERT(ecm_front_end_ipv6_accelerated_count >= 0, "Bad accel counter\n");
	spin_unlock_bh(&ecm_front_end_ipv6_lock);
}

/*
 * ecm_front_end_ipv6_connection_non_ported_front_end_ref()
 *	Ref a connection front end instance
 */
static void ecm_front_end_ipv6_connection_non_ported_front_end_ref(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;

	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);
	spin_lock_bh(&fecnpi->lock);
	fecnpi->refs++;
	DEBUG_TRACE("%p: fecnpi ref %d\n", fecnpi, fecnpi->refs);
	DEBUG_ASSERT(fecnpi->refs > 0, "%p: ref wrap\n", fecnpi);
	spin_unlock_bh(&fecnpi->lock);
}

/*
 * ecm_front_end_ipv6_connection_non_ported_front_end_deref()
 *	Deref a connection front end instance
 */
static int ecm_front_end_ipv6_connection_non_ported_front_end_deref(struct ecm_front_end_connection_instance *feci)
{
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;

	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);

	spin_lock_bh(&fecnpi->lock);
	fecnpi->refs--;
	DEBUG_ASSERT(fecnpi->refs >= 0, "%p: ref wrap\n", fecnpi);

	if (fecnpi->refs > 0) {
		int refs = fecnpi->refs;
		spin_unlock_bh(&fecnpi->lock);
		DEBUG_TRACE("%p: fecnpi deref %d\n", fecnpi, refs);
		return refs;
	}
	spin_unlock_bh(&fecnpi->lock);

	/*
	 * We can now destroy the instance
	 */
	DEBUG_TRACE("%p: fecnpi final\n", fecnpi);
	DEBUG_CLEAR_MAGIC(fecnpi);
	kfree(fecnpi);

	return 0;
}

/*
 * ecm_front_end_ipv6_connection_non_ported_front_end_xml_state_get()
 *	Return an XML element containing the state of this Non ported front end instance
 */
static int ecm_front_end_ipv6_connection_non_ported_front_end_xml_state_get(struct ecm_front_end_connection_instance *feci, char *buf, int buf_sz)
{
	bool can_accel;
	ecm_front_end_acceleration_mode_t accel_mode;
	struct ecm_front_end_connection_mode_stats stats;
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)feci;

	DEBUG_CHECK_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC, "%p: magic failed", fecnpi);

	spin_lock_bh(&fecnpi->lock);
	can_accel = fecnpi->can_accel;
	accel_mode = fecnpi->accel_mode;
	memcpy(&stats, &feci->stats, sizeof(struct ecm_front_end_connection_mode_stats));
	spin_unlock_bh(&fecnpi->lock);

	return snprintf(buf, buf_sz, "<front_end_tcp can_accel=\"%d\" accel_mode=\"%d\" decelerate_pending=\"%d\""
			" no_action_seen_total=\"%d\" no_action_seen=\"%d\" no_action_seen_limit=\"%d\""
			" driver_fail_total=\"%d\" driver_fail=\"%d\" driver_fail_limit=\"%d\""
			" nss_nack_total=\"%d\" nss_nack=\"%d\" nss_nack_limit=\"%d\"/>\n",
			can_accel,
			accel_mode,
			stats.decelerate_pending,
			stats.no_action_seen_total,
			stats.no_action_seen,
			stats.no_action_seen_limit,
			stats.driver_fail_total,
			stats.driver_fail,
			stats.driver_fail_limit,
			stats.nss_nack_total,
			stats.nss_nack,
			stats.nss_nack_limit);

}

/*
 * ecm_front_end_ipv6_connection_non_ported_instance_alloc()
 *	Create a front end instance specific for NON_PORTED connection
 */
static struct ecm_front_end_ipv6_connection_non_ported_instance *ecm_front_end_ipv6_connection_non_ported_instance_alloc(
								struct ecm_db_connection_instance *ci,
								struct ecm_db_mapping_instance *src_mi,
								struct ecm_db_mapping_instance *dest_mi,
								bool can_accel)
{
	struct ecm_front_end_ipv6_connection_non_ported_instance *fecnpi;

	fecnpi = (struct ecm_front_end_ipv6_connection_non_ported_instance *)kzalloc(sizeof(struct ecm_front_end_ipv6_connection_non_ported_instance), GFP_ATOMIC | __GFP_NOWARN);
	if (!fecnpi) {
		DEBUG_WARN("NON_PORTED Front end alloc failed\n");
		return NULL;
	}

	/*
	 * Refs is 1 for the creator of the connection
	 */
	fecnpi->refs = 1;
	DEBUG_SET_MAGIC(fecnpi, ECM_FRONT_END_IPV6_CONNECTION_NON_PORTED_INSTANCE_MAGIC);
	spin_lock_init(&fecnpi->lock);

	fecnpi->can_accel = can_accel;
	fecnpi->accel_mode = (can_accel) ? ECM_FRONT_END_ACCELERATION_MODE_DECEL : ECM_FRONT_END_ACCELERATION_MODE_FAIL_DENIED;
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	fecnpi->base.stats.no_action_seen_limit = ecm_front_end_ipv6_no_action_limit_default;
	fecnpi->base.stats.driver_fail_limit = ecm_front_end_ipv6_driver_fail_limit_default;
	fecnpi->base.stats.nss_nack_limit = ecm_front_end_ipv6_nack_limit_default;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Copy reference to connection - no need to ref ci as ci maintains a ref to this instance instead (this instance persists for as long as ci does)
	 */
	fecnpi->ci = ci;

	/*
	 * Populate the methods and callbacks
	 */
	fecnpi->base.ref = ecm_front_end_ipv6_connection_non_ported_front_end_ref;
	fecnpi->base.deref = ecm_front_end_ipv6_connection_non_ported_front_end_deref;
	fecnpi->base.decelerate = ecm_front_end_ipv6_connection_non_ported_front_end_decelerate;
	fecnpi->base.accel_state_get = ecm_front_end_ipv6_connection_non_ported_front_end_accel_state_get;
	fecnpi->base.action_seen = ecm_front_end_ipv6_connection_non_ported_front_end_action_seen;
	fecnpi->base.accel_ceased = ecm_front_end_ipv6_connection_non_ported_front_end_accel_ceased;
	fecnpi->base.xml_state_get = ecm_front_end_ipv6_connection_non_ported_front_end_xml_state_get;

	return fecnpi;
}

/*
 * ecm_front_end_ipv6_assign_classifier()
 *	Instantiate and assign classifier of type upon the connection, also returning it if it could be allocated.
 */
static struct ecm_classifier_instance *ecm_front_end_ipv6_assign_classifier(struct ecm_db_connection_instance *ci, ecm_classifier_type_t type)
{
	DEBUG_TRACE("%p: Assign classifier of type: %d\n", ci, type);
	DEBUG_ASSERT(type != ECM_CLASSIFIER_TYPE_DEFAULT, "Must never need to instantiate default type in this way");

	if (type == ECM_CLASSIFIER_TYPE_NL) {
		struct ecm_classifier_nl_instance *cnli;
		cnli = ecm_classifier_nl_instance_alloc(ci);
		if (!cnli) {
			DEBUG_TRACE("%p: Failed to create Netlink classifier\n", ci);
			return NULL;
		}
		DEBUG_TRACE("%p: Created Netlink classifier: %p\n", ci, cnli);
		ecm_db_connection_classifier_assign(ci, (struct ecm_classifier_instance *)cnli);
		return (struct ecm_classifier_instance *)cnli;
	}

	if (type == ECM_CLASSIFIER_TYPE_DSCP) {
		struct ecm_classifier_dscp_instance *cdscpi;
		cdscpi = ecm_classifier_dscp_instance_alloc(ci);
		if (!cdscpi) {
			DEBUG_TRACE("%p: Failed to create DSCP classifier\n", ci);
			return NULL;
		}
		DEBUG_TRACE("%p: Created DSCP classifier: %p\n", ci, cdscpi);
		ecm_db_connection_classifier_assign(ci, (struct ecm_classifier_instance *)cdscpi);
		return (struct ecm_classifier_instance *)cdscpi;
	}

#ifdef ECM_CLASSIFIER_HYFI_ENABLE
	if (type == ECM_CLASSIFIER_TYPE_HYFI) {
		struct ecm_classifier_hyfi_instance *chfi;
		chfi = ecm_classifier_hyfi_instance_alloc(ci);
		if (!chfi) {
			DEBUG_TRACE("%p: Failed to create HyFi classifier\n", ci);
			return NULL;
		}
		DEBUG_TRACE("%p: Created HyFi classifier: %p\n", ci, chfi);
		ecm_db_connection_classifier_assign(ci, (struct ecm_classifier_instance *)chfi);
		return (struct ecm_classifier_instance *)chfi;
	}
#endif

	// GGG TODO Add other classifier types.
	DEBUG_ASSERT(NULL, "%p: Unsupported type: %d\n", ci, type);
	return NULL;
}

/*
 * ecm_front_end_ipv6_reclassify()
 *	Signal reclassify upon the assigned classifiers.
 *
 * Classifiers that unassigned themselves we TRY to re-instantiate them.
 * Returns false if the function is not able to instantiate all missing classifiers.
 * This function does not release and references to classifiers in the assignments[].
 */
static bool ecm_front_end_ipv6_reclassify(struct ecm_db_connection_instance *ci, int assignment_count, struct ecm_classifier_instance *assignments[])
{
	ecm_classifier_type_t classifier_type;
	int i;
	bool full_reclassification = true;

	/*
	 * assignment_count will always be <= the number of classifier types available
	 */
	for (i = 0, classifier_type = ECM_CLASSIFIER_TYPE_DEFAULT; i < assignment_count; ++i, ++classifier_type) {
		ecm_classifier_type_t aci_type;
		struct ecm_classifier_instance *aci;

		aci = assignments[i];
		aci_type = aci->type_get(aci);
		DEBUG_TRACE("%p: Reclassify: %d\n", ci, aci_type);
		aci->reclassify(aci);

		/*
		 * If the connection has a full complement of assigned classifiers then these will match 1:1 with the classifier_type (all in same order).
		 * If not, we have to create the missing ones.
		 */
		if (aci_type == classifier_type) {
			continue;
		}

		/*
		 * Need to instantiate the missing classifier types until we get to the same type as aci_type then we are back in sync to continue reclassification
		 */
		while (classifier_type != aci_type) {
			struct ecm_classifier_instance *naci;
			DEBUG_TRACE("%p: Instantiate missing type: %d\n", ci, classifier_type);
			DEBUG_ASSERT(classifier_type < ECM_CLASSIFIER_TYPES, "Algorithm bad");

			naci = ecm_front_end_ipv6_assign_classifier(ci, classifier_type);
			if (!naci) {
				full_reclassification = false;
			} else {
				naci->deref(naci);
			}

			classifier_type++;
		}
	}

	/*
	 * Add missing types
	 */
	for (; classifier_type < ECM_CLASSIFIER_TYPES; ++classifier_type) {
		struct ecm_classifier_instance *naci;
		DEBUG_TRACE("%p: Instantiate missing type: %d\n", ci, classifier_type);

		naci = ecm_front_end_ipv6_assign_classifier(ci, classifier_type);
		if (!naci) {
			full_reclassification = false;
		} else {
			naci->deref(naci);
		}
	}

	DEBUG_TRACE("%p: reclassify done: %u\n", ci, full_reclassification);
	return full_reclassification;
}


/*
 * ecm_front_end_ipv6_tcp_process()
 *	Process a TCP packet
 */
static unsigned int ecm_front_end_ipv6_tcp_process(struct net_device *out_dev,
							struct net_device *in_dev,
							uint8_t *src_node_addr,
							uint8_t *dest_node_addr,
							bool can_accel,  bool is_routed, struct sk_buff *skb,
							struct ecm_tracker_ip_header *iph,
							struct nf_conn *ct, enum ip_conntrack_dir ct_dir, ecm_db_direction_t ecm_dir,
							struct nf_conntrack_tuple *orig_tuple, struct nf_conntrack_tuple *reply_tuple,
							ip_addr_t ip_src_addr, ip_addr_t ip_dest_addr)
{
	struct tcphdr *tcp_hdr;
	struct tcphdr tcp_hdr_buff;
	int src_port;
	int dest_port;
	struct ecm_db_connection_instance *ci;
	ecm_tracker_sender_type_t sender;
	ip_addr_t match_addr;
	struct ecm_classifier_instance *assignments[ECM_CLASSIFIER_TYPES];
	int aci_index;
	int assignment_count;
	ecm_db_timer_group_t ci_orig_timer_group;
	struct ecm_classifier_process_response prevalent_pr;

	/*
	 * Extract TCP header to obtain port information
	 */
	tcp_hdr = ecm_tracker_tcp_check_header_and_read(skb, iph, &tcp_hdr_buff);
	if (unlikely(!tcp_hdr)) {
		DEBUG_WARN("TCP packet header %p\n", skb);
		return NF_ACCEPT;
	}

	/*
	 * Now extract information, if we have conntrack then use that (which would already be in the tuples)
	 */
	if (unlikely(!ct)) {
		orig_tuple->src.u.tcp.port = tcp_hdr->source;
		orig_tuple->dst.u.tcp.port = tcp_hdr->dest;
		reply_tuple->src.u.tcp.port = tcp_hdr->dest;
		reply_tuple->dst.u.tcp.port = tcp_hdr->source;
	}

	/*
	 * Extract transport port information
	 * Refer to the ecm_front_end_ipv6_process() for information on how we extract this information.
	 */
	if (ct_dir == IP_CT_DIR_ORIGINAL) {
		switch(ecm_dir) {
		case ECM_DB_DIRECTION_NON_NAT:
		case ECM_DB_DIRECTION_BRIDGED:
			src_port = ntohs(orig_tuple->src.u.tcp.port);
			dest_port = ntohs(orig_tuple->dst.u.tcp.port);
			break;
		default:
			DEBUG_ASSERT(false, "Unhandled ecm_dir: %d\n", ecm_dir);
		}
	} else {
		switch(ecm_dir) {
		case ECM_DB_DIRECTION_NON_NAT:
		case ECM_DB_DIRECTION_BRIDGED:
			dest_port = ntohs(orig_tuple->src.u.tcp.port);
			src_port = ntohs(orig_tuple->dst.u.tcp.port);
			break;
		default:
			DEBUG_ASSERT(false, "Unhandled ecm_dir: %d\n", ecm_dir);
		}
	}

	DEBUG_TRACE("TCP src: " ECM_IP_ADDR_OCTAL_FMT ":%d, dest: " ECM_IP_ADDR_OCTAL_FMT ":%d, dir %d\n",
			ECM_IP_ADDR_TO_OCTAL(ip_src_addr), src_port, ECM_IP_ADDR_TO_OCTAL(ip_dest_addr), dest_port, ecm_dir);

	/*
	 * Look up a connection
	 */
	ci = ecm_db_connection_find_and_ref(ip_src_addr, ip_dest_addr, IPPROTO_TCP, src_port, dest_port);

	/*
	 * If there is no existing connection then create a new one.
	 */
	if (unlikely(!ci)) {
		struct ecm_db_mapping_instance *src_mi;
		struct ecm_db_mapping_instance *dest_mi;
		struct ecm_classifier_default_instance *dci;
		struct ecm_db_connection_instance *nci;
		ecm_classifier_type_t classifier_type;
		struct ecm_front_end_connection_instance *feci;
		int32_t to_list_first;
		struct ecm_db_iface_instance *to_list[ECM_DB_IFACE_HEIRARCHY_MAX];
		int32_t from_list_first;
		struct ecm_db_iface_instance *from_list[ECM_DB_IFACE_HEIRARCHY_MAX];

		DEBUG_INFO("New TCP connection from " ECM_IP_ADDR_OCTAL_FMT ":%u to " ECM_IP_ADDR_OCTAL_FMT ":%u\n",
				ECM_IP_ADDR_TO_OCTAL(ip_src_addr), src_port, ECM_IP_ADDR_TO_OCTAL(ip_dest_addr), dest_port);

		/*
		 * Do not add a connection in terminating state
		 */
		if (ct) {
			spin_lock_bh(&ct->lock);
			if (ct->proto.tcp.state >= TCP_CONNTRACK_FIN_WAIT && ct->proto.tcp.state <= TCP_CONNTRACK_CLOSE) {
				spin_unlock_bh(&ct->lock);
				DEBUG_TRACE("%p: Connection in termination state %#X\n", ct, ct->proto.tcp.state);
				return NF_ACCEPT;
			}
			spin_unlock_bh(&ct->lock);
		}

		/*
		 * Before we attempt to create the connection are we being terminated?
		 */
		spin_lock_bh(&ecm_front_end_ipv6_lock);
		if (ecm_front_end_ipv6_terminate_pending) {
			spin_unlock_bh(&ecm_front_end_ipv6_lock);
			DEBUG_WARN("Terminating\n");

			/*
			 * As we are terminating we just allow the packet to pass - it's no longer our concern
			 */
			return NF_ACCEPT;
		}
		spin_unlock_bh(&ecm_front_end_ipv6_lock);

		/*
		 * Now allocate the new connection
		 */
		nci = ecm_db_connection_alloc();
		if (!nci) {
			DEBUG_WARN("Failed to allocate connection\n");
			return NF_ACCEPT;
		}

		/*
		 * Get the src and destination mappings
		 * For this we also need the interface lists which we also set upon the new connection while we are at it.
		 * GGG TODO rework terms of "src/dest" - these need to be named consistently as from/to as per database terms.
		 * GGG TODO The empty list checks should not be needed, mapping_establish_and_ref() should fail out if there is no list anyway.
		 */
		DEBUG_TRACE("%p: Create the 'from' interface heirarchy list\n", nci);
		from_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(from_list, ip_dest_addr, ip_src_addr, IPPROTO_TCP, in_dev, is_routed, in_dev, src_node_addr, dest_node_addr);
		if (from_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to obtain 'from' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_from_interfaces_reset(nci, from_list, from_list_first);

		DEBUG_TRACE("%p: Create source mapping\n", nci);
		src_mi = ecm_front_end_ipv6_mapping_establish_and_ref(in_dev, ip_src_addr, src_port, from_list, from_list_first, src_node_addr);
		ecm_db_connection_interfaces_deref(from_list, from_list_first);
		if (!src_mi) {
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to establish src mapping\n");
			return NF_ACCEPT;
		}

		DEBUG_TRACE("%p: Create the 'to' interface heirarchy list\n", nci);
		to_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(to_list, ip_src_addr, ip_dest_addr, IPPROTO_TCP, out_dev, is_routed, in_dev, dest_node_addr, src_node_addr);
		if (to_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to obtain 'to' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_to_interfaces_reset(nci, to_list, to_list_first);

		DEBUG_TRACE("%p: Create dest mapping\n", nci);
		dest_mi = ecm_front_end_ipv6_mapping_establish_and_ref(out_dev, ip_dest_addr, dest_port, to_list, to_list_first, dest_node_addr);
		ecm_db_connection_interfaces_deref(to_list, to_list_first);
		if (!dest_mi) {
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to establish dest mapping\n");
			return NF_ACCEPT;
		}

		/*
		 * Connection must have a front end instance associated with it
		 */
		feci = (struct ecm_front_end_connection_instance *)ecm_front_end_ipv6_connection_tcp_instance_alloc(nci, src_mi, dest_mi, can_accel);
		if (!feci) {
			ecm_db_mapping_deref(dest_mi);
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to allocate front end\n");
			return NF_ACCEPT;
		}

		/*
		 * Every connection also needs a default classifier which is considered 'special'
		 */
		dci = ecm_classifier_default_instance_alloc(nci, IPPROTO_TCP, ecm_dir, src_port, dest_port);
		if (!dci) {
			feci->deref(feci);
			ecm_db_mapping_deref(dest_mi);
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to allocate default classifier\n");
			return NF_ACCEPT;
		}

		/*
		 * Every connection starts with a full complement of classifiers assigned.
		 * NOTE: Default classifier is a special case considered previously
		 */
		for (classifier_type = ECM_CLASSIFIER_TYPE_DEFAULT + 1; classifier_type < ECM_CLASSIFIER_TYPES; ++classifier_type) {
			struct ecm_classifier_instance *aci = ecm_front_end_ipv6_assign_classifier(nci, classifier_type);
			if (aci) {
				aci->deref(aci);
			} else {
				dci->base.deref((struct ecm_classifier_instance *)dci);
				feci->deref(feci);
				ecm_db_mapping_deref(dest_mi);
				ecm_db_mapping_deref(src_mi);
				ecm_db_connection_deref(nci);
				DEBUG_WARN("Failed to allocate classifiers assignments\n");
				return NF_ACCEPT;
			}
		}

		/*
		 * Now add the connection into the database.
		 * NOTE: In an SMP situation such as ours there is a possibility that more than one packet for the same
		 * connection is being processed simultaneously.
		 * We *could* end up creating more than one connection instance for the same actual connection.
		 * To guard against this we now perform a mutex'd lookup of the connection + add once more - another cpu may have created it before us.
		 */
		spin_lock_bh(&ecm_front_end_ipv6_lock);
		ci = ecm_db_connection_find_and_ref(ip_src_addr, ip_dest_addr, IPPROTO_TCP, src_port, dest_port);
		if (ci) {
			/*
			 * Another cpu created the same connection before us - use the one we just found
			 */
			spin_unlock_bh(&ecm_front_end_ipv6_lock);
			ecm_db_connection_deref(nci);
		} else {
			ecm_db_timer_group_t tg;
			ecm_tracker_sender_state_t src_state;
			ecm_tracker_sender_state_t dest_state;
			ecm_tracker_connection_state_t state;
			struct ecm_tracker_instance *ti;

			/*
			 * Ask tracker for timer group to set the connection to initially.
			 */
			ti = dci->tracker_get_and_ref(dci);
			ti->state_get(ti, &src_state, &dest_state, &state, &tg);
			ti->deref(ti);

			/*
			 * Add the new connection we created into the database
			 * NOTE: assign to a short timer group for now - it is the assigned classifiers responsibility to do this
			 */
			ecm_db_connection_add(nci, feci, dci,
					src_mi, dest_mi, src_mi, dest_mi,
					IPPROTO_TCP, ecm_dir,
					NULL /* final callback */,
					tg, is_routed, nci);

			spin_unlock_bh(&ecm_front_end_ipv6_lock);

			ci = nci;
			DEBUG_INFO("%p: New TCP connection created\n", ci);
		}

		/*
		 * No longer need referenecs to the objects we created
		 */
		feci->deref(feci);
		dci->base.deref((struct ecm_classifier_instance *)dci);
		ecm_db_mapping_deref(src_mi);
		ecm_db_mapping_deref(dest_mi);
	}

	/*
	 * Keep connection alive as we have seen activity
	 */
	if (!ecm_db_connection_defunct_timer_touch(ci)) {
		ecm_db_connection_deref(ci);
		return NF_ACCEPT;
	}

	/*
	 * Do we need to action generation change?
	 */
	if (unlikely(ecm_db_connection_classifier_generation_changed(ci))) {
		int i;
		bool reclassify_allowed;
		int32_t to_list_first;
		struct ecm_db_iface_instance *to_list[ECM_DB_IFACE_HEIRARCHY_MAX];
		int32_t from_list_first;
		struct ecm_db_iface_instance *from_list[ECM_DB_IFACE_HEIRARCHY_MAX];

		DEBUG_INFO("%p: re-gen needed\n", ci);

		/*
		 * Update the interface lists - these may have changed, e.g. LAG path change etc.
		 * NOTE: We never have to change the usual mapping->host->node_iface arrangements for each side of the connection (to/from sides)
		 * This is because if these interfaces change then the connection is dead anyway.
		 * But a LAG slave might change the heirarchy the connection is using but the LAG master is still sane.
		 * GGG TODO The empty list checks may mean that stale interface list information remains on a connection - this could be bad.
		 * GGG Investigate the removal of the empty list checks.
		 */
		DEBUG_TRACE("%p: Update the 'from' interface heirarchy list\n", ci);
		from_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(from_list, ip_dest_addr, ip_src_addr, IPPROTO_TCP, in_dev, is_routed, in_dev, src_node_addr, dest_node_addr);
		if (from_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(ci);
			DEBUG_WARN("Failed to obtain 'from' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_from_interfaces_reset(ci, from_list, from_list_first);
		ecm_db_connection_interfaces_deref(from_list, from_list_first);

		DEBUG_TRACE("%p: Update the 'to' interface heirarchy list\n", ci);
		to_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(to_list, ip_src_addr, ip_dest_addr, IPPROTO_TCP, out_dev, is_routed, in_dev, dest_node_addr, src_node_addr);
		if (to_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(ci);
			DEBUG_WARN("Failed to obtain 'to' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_to_interfaces_reset(ci, to_list, to_list_first);
		ecm_db_connection_interfaces_deref(to_list, to_list_first);

		/*
		 * Get list of assigned classifiers to reclassify.
		 * Remember: This also includes our default classifier too.
		 */
		assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(ci, assignments);

		/*
		 * All of the assigned classifiers must permit reclassification.
		 */
		reclassify_allowed = true;
		for (i = 0; i < assignment_count; ++i) {
			DEBUG_TRACE("%p: Calling to reclassify: %p, type: %d\n", ci, assignments[i], assignments[i]->type_get(assignments[i]));
			if (!assignments[i]->reclassify_allowed(assignments[i])) {
				DEBUG_TRACE("%p: reclassify denied: %p, by type: %d\n", ci, assignments[i], assignments[i]->type_get(assignments[i]));
				reclassify_allowed = false;
				break;
			}
		}

		if (!reclassify_allowed) {
			DEBUG_WARN("%p: re-gen denied\n", ci);
		} else {
			/*
			 * Reclassify
			 */
			DEBUG_TRACE("%p: reclassify\n", ci);
			if (!ecm_front_end_ipv6_reclassify(ci, assignment_count, assignments)) {
				DEBUG_WARN("%p: Regeneration failed, dropping packet\n", ci);
				ecm_db_connection_assignments_release(assignment_count, assignments);
				ecm_db_connection_deref(ci);
				return NF_ACCEPT;
			}
			DEBUG_TRACE("%p: reclassify success\n", ci);
		}

		/*
		 * Release the assignments and re-obtain them as there may be new ones been reassigned.
		 */
		ecm_db_connection_assignments_release(assignment_count, assignments);
	}

	/*
	 * Identify which side of the connection is sending
	 */
	ecm_db_connection_from_address_get(ci, match_addr);
	if (ECM_IP_ADDR_MATCH(ip_src_addr, match_addr)) {
		sender = ECM_TRACKER_SENDER_TYPE_SRC;
	} else {
		sender = ECM_TRACKER_SENDER_TYPE_DEST;
	}

	/*
	 * Iterate the assignments and call to process!
	 * Policy implemented:
	 * 1. Classifiers that say they are not relevant are unassigned and not actioned further.
	 * 2. Any drop command from any classifier is honoured.
	 * 3. All classifiers must action acceleration for accel to be honoured, any classifiers not sure of their relevance will stop acceleration.
	 * 4. Only the highest priority classifier, that actions it, will have its qos tag honoured.
	 * 5. Only the highest priority classifier, that actions it, will have its timer group honoured.
	 */
	DEBUG_TRACE("%p: process begin, skb: %p\n", ci, skb);
	prevalent_pr.process_actions = 0;
	prevalent_pr.drop = false;
	prevalent_pr.flow_qos_tag = skb->priority;
	prevalent_pr.return_qos_tag = skb->priority;
	prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_ACCEL;
	prevalent_pr.timer_group = ci_orig_timer_group = ecm_db_connection_timer_group_get(ci);

	assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(ci, assignments);
	for (aci_index = 0; aci_index < assignment_count; ++aci_index) {
		struct ecm_classifier_process_response aci_pr;
		struct ecm_classifier_instance *aci;

		aci = assignments[aci_index];
		DEBUG_TRACE("%p: process: %p, type: %d\n", ci, aci, aci->type_get(aci));
		aci->process(aci, sender, iph, skb, &aci_pr);
		DEBUG_TRACE("%p: aci_pr: process actions: %x, became relevant: %u, relevance: %d, drop: %d, "
				"flow_qos_tag: %u, return_qos_tag: %u, accel_mode: %x, timer_group: %d\n",
				ci, aci_pr.process_actions, aci_pr.became_relevant, aci_pr.relevance, aci_pr.drop,
				aci_pr.flow_qos_tag, aci_pr.return_qos_tag, aci_pr.accel_mode, aci_pr.timer_group);

		if (aci_pr.relevance == ECM_CLASSIFIER_RELEVANCE_NO) {
			ecm_classifier_type_t aci_type;

			/*
			 * This classifier can be unassigned - PROVIDED it is not the default classifier
			 */
			aci_type = aci->type_get(aci);
			if (aci_type == ECM_CLASSIFIER_TYPE_DEFAULT) {
				continue;
			}

			DEBUG_INFO("%p: Classifier not relevant, unassign: %d", ci, aci_type);
			ecm_db_connection_classifier_unassign(ci, aci);
			continue;
		}

		/*
		 * Yes or Maybe relevant.
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DROP) {
			/*
			 * Drop command from any classifier is actioned.
			 */
			DEBUG_TRACE("%p: wants drop: %p, type: %d, skb: %p\n", ci, aci, aci->type_get(aci), skb);
			prevalent_pr.drop |= aci_pr.drop;
		}

		/*
		 * Accel mode permission
		 */
		if (aci_pr.relevance == ECM_CLASSIFIER_RELEVANCE_MAYBE) {
			/*
			 * Classifier not sure of its relevance - cannot accel yet
			 */
			DEBUG_TRACE("%p: accel denied by maybe: %p, type: %d\n", ci, aci, aci->type_get(aci));
			prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_NO;
		} else {
			if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_ACCEL_MODE) {
				if (aci_pr.accel_mode == ECM_CLASSIFIER_ACCELERATION_MODE_NO) {
					DEBUG_TRACE("%p: accel denied: %p, type: %d\n", ci, aci, aci->type_get(aci));
					prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_NO;
				}
				/* else yes or don't care about accel */
			}
		}

		/*
		 * Timer group (the last classifier i.e. the highest priority one) will 'win'
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_TIMER_GROUP) {
			DEBUG_TRACE("%p: timer group: %p, type: %d, group: %d\n", ci, aci, aci->type_get(aci), aci_pr.timer_group);
			prevalent_pr.timer_group = aci_pr.timer_group;
		}

		/*
		 * Qos tag (the last classifier i.e. the highest priority one) will 'win'
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_QOS_TAG) {
			DEBUG_TRACE("%p: aci: %p, type: %d, flow qos tag: %u, return qos tag: %u\n",
					ci, aci, aci->type_get(aci), aci_pr.flow_qos_tag, aci_pr.return_qos_tag);
			prevalent_pr.flow_qos_tag = aci_pr.flow_qos_tag;
			prevalent_pr.return_qos_tag = aci_pr.return_qos_tag;
		}

		/*
		 * If any classifier denied DSCP remarking then that overrides every classifier
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY) {
			DEBUG_TRACE("%p: aci: %p, type: %d, DSCP remark denied\n",
					ci, aci, aci->type_get(aci));
			prevalent_pr.process_actions |= ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY;
			prevalent_pr.process_actions &= ~ECM_CLASSIFIER_PROCESS_ACTION_DSCP;
		}

		/*
		 * DSCP remark action, but only if it has not been denied by any classifier
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP) {
			if (!(prevalent_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY)) {
				DEBUG_TRACE("%p: aci: %p, type: %d, DSCP remark wanted, flow_dscp: %u, return dscp: %u\n",
						ci, aci, aci->type_get(aci), aci_pr.flow_dscp, aci_pr.return_dscp);
				prevalent_pr.process_actions |= ECM_CLASSIFIER_PROCESS_ACTION_DSCP;
				prevalent_pr.flow_dscp = aci_pr.flow_dscp;
				prevalent_pr.return_dscp = aci_pr.return_dscp;
			}
		}
	}
	ecm_db_connection_assignments_release(assignment_count, assignments);

	/*
	 * Change timer group?
	 */
	if (ci_orig_timer_group != prevalent_pr.timer_group) {
		DEBUG_TRACE("%p: change timer group from: %d to: %d\n", ci, ci_orig_timer_group, prevalent_pr.timer_group);
		ecm_db_connection_defunct_timer_reset(ci, prevalent_pr.timer_group);
	}

	/*
	 * Drop?
	 */
	if (prevalent_pr.drop) {
		DEBUG_TRACE("%p: drop: %p\n", ci, skb);
		ecm_db_connection_data_totals_update_dropped(ci, (sender == ECM_TRACKER_SENDER_TYPE_SRC)? true : false, skb->len, 1);
		ecm_db_connection_deref(ci);
		return NF_ACCEPT;
	}
	ecm_db_connection_data_totals_update(ci, (sender == ECM_TRACKER_SENDER_TYPE_SRC)? true : false, skb->len, 1);

	/*
	 * Assign qos tag
	 * GGG TODO Should we use sender to identify whether to use flow or return qos tag?
	 */
	skb->priority = prevalent_pr.flow_qos_tag;
	DEBUG_TRACE("%p: skb priority: %u\n", ci, skb->priority);

	/*
	 * Accelerate?
	 */
	if (prevalent_pr.accel_mode == ECM_CLASSIFIER_ACCELERATION_MODE_ACCEL) {
		struct ecm_front_end_connection_instance *feci;
		DEBUG_TRACE("%p: accel\n", ci);
		feci = ecm_db_connection_front_end_get_and_ref(ci);
		ecm_front_end_ipv6_connection_tcp_front_end_accelerate(feci, &prevalent_pr, ct);
		feci->deref(feci);
	}
	ecm_db_connection_deref(ci);

	return NF_ACCEPT;
}

/*
 * ecm_front_end_ipv6_udp_process()
 *	Process a UDP packet
 */
static unsigned int ecm_front_end_ipv6_udp_process(struct net_device *out_dev,
							struct net_device *in_dev,
							uint8_t *src_node_addr,
							uint8_t *dest_node_addr,
							bool can_accel, bool is_routed, struct sk_buff *skb,
							struct ecm_tracker_ip_header *iph,
							struct nf_conn *ct, enum ip_conntrack_dir ct_dir, ecm_db_direction_t ecm_dir,
							struct nf_conntrack_tuple *orig_tuple, struct nf_conntrack_tuple *reply_tuple,
							ip_addr_t ip_src_addr, ip_addr_t ip_dest_addr)
{
	struct udphdr *udp_hdr;
	struct udphdr udp_hdr_buff;
	int src_port;
	int dest_port;
	struct ecm_db_connection_instance *ci;
	ecm_tracker_sender_type_t sender;
	ip_addr_t match_addr;
	struct ecm_classifier_instance *assignments[ECM_CLASSIFIER_TYPES];
	int aci_index;
	int assignment_count;
	ecm_db_timer_group_t ci_orig_timer_group;
	struct ecm_classifier_process_response prevalent_pr;

	/*
	 * Extract UDP header to obtain port information
	 */
	udp_hdr = ecm_tracker_udp_check_header_and_read(skb, iph, &udp_hdr_buff);
	if (unlikely(!udp_hdr)) {
		DEBUG_WARN("Invalid UDP header in skb %p\n", skb);
		return NF_ACCEPT;
	}

	/*
	 * Deny acceleration for L2TP-over-UDP tunnel
	 */
	if (skb->sk) {
		if(skb->sk->sk_protocol == IPPROTO_UDP) {
			struct udp_sock *usk = udp_sk(skb->sk);
			if (usk) {
				if (unlikely(usk->encap_type == UDP_ENCAP_L2TPINUDP)) {
					DEBUG_TRACE("Skip packets for L2TP tunnel in skb %p\n", skb);
					can_accel = false;
				}
			}
		}
	}

	/*
	 * Now extract information, if we have conntrack then use that (which would already be in the tuples)
	 */
	if (unlikely(!ct)) {
		orig_tuple->src.u.udp.port = udp_hdr->source;
		orig_tuple->dst.u.udp.port = udp_hdr->dest;
		reply_tuple->src.u.udp.port = udp_hdr->dest;
		reply_tuple->dst.u.udp.port = udp_hdr->source;
	}

	/*
	 * Extract transport port information
	 * Refer to the ecm_front_end_ipv6_process() for information on how we extract this information.
	 */
	if (ct_dir == IP_CT_DIR_ORIGINAL) {
		switch(ecm_dir) {
		case ECM_DB_DIRECTION_NON_NAT:
		case ECM_DB_DIRECTION_BRIDGED:
			src_port = ntohs(orig_tuple->src.u.udp.port);
			dest_port = ntohs(orig_tuple->dst.u.udp.port);
			break;
		default:
			DEBUG_ASSERT(false, "Unhandled ecm_dir: %d\n", ecm_dir);
		}
	} else {
		switch(ecm_dir) {
		case ECM_DB_DIRECTION_NON_NAT:
		case ECM_DB_DIRECTION_BRIDGED:
			dest_port = ntohs(orig_tuple->src.u.udp.port);
			src_port = ntohs(orig_tuple->dst.u.udp.port);
			break;
		default:
			DEBUG_ASSERT(false, "Unhandled ecm_dir: %d\n", ecm_dir);
		}
	}
	DEBUG_TRACE("UDP src: " ECM_IP_ADDR_OCTAL_FMT ":%d, dest: " ECM_IP_ADDR_OCTAL_FMT ":%d, dir %d\n",
			ECM_IP_ADDR_TO_OCTAL(ip_src_addr), src_port, ECM_IP_ADDR_TO_OCTAL(ip_dest_addr), dest_port, ecm_dir);

	/*
	 * Look up a connection
	 */
	ci = ecm_db_connection_find_and_ref(ip_src_addr, ip_dest_addr, IPPROTO_UDP, src_port, dest_port);

	/*
	 * If there is no existing connection then create a new one.
	 */
	if (unlikely(!ci)) {
		struct ecm_db_mapping_instance *src_mi;
		struct ecm_db_mapping_instance *dest_mi;
		struct ecm_classifier_default_instance *dci;
		struct ecm_front_end_connection_instance *feci;
		struct ecm_db_connection_instance *nci;
		ecm_classifier_type_t classifier_type;
		int32_t to_list_first;
		struct ecm_db_iface_instance *to_list[ECM_DB_IFACE_HEIRARCHY_MAX];
		int32_t from_list_first;
		struct ecm_db_iface_instance *from_list[ECM_DB_IFACE_HEIRARCHY_MAX];

		DEBUG_INFO("New UDP connection from " ECM_IP_ADDR_OCTAL_FMT ":%u to " ECM_IP_ADDR_OCTAL_FMT ":%u\n",
				ECM_IP_ADDR_TO_OCTAL(ip_src_addr), src_port, ECM_IP_ADDR_TO_OCTAL(ip_dest_addr), dest_port);

		/*
		 * Before we attempt to create the connection are we being terminated?
		 */
		spin_lock_bh(&ecm_front_end_ipv6_lock);
		if (ecm_front_end_ipv6_terminate_pending) {
			spin_unlock_bh(&ecm_front_end_ipv6_lock);
			DEBUG_WARN("Terminating\n");

			/*
			 * As we are terminating we just allow the packet to pass - it's no longer our concern
			 */
			return NF_ACCEPT;
		}
		spin_unlock_bh(&ecm_front_end_ipv6_lock);

		/*
		 * Now allocate the new connection
		 */
		nci = ecm_db_connection_alloc();
		if (!nci) {
			DEBUG_WARN("Failed to allocate connection\n");
			return NF_ACCEPT;
		}

		/*
		 * Get the src and destination mappings
		 * For this we also need the interface lists which we also set upon the new connection while we are at it.
		 * GGG TODO rework terms of "src/dest" - these need to be named consistently as from/to as per database terms.
		 * GGG TODO The empty list checks should not be needed, mapping_establish_and_ref() should fail out if there is no list anyway.
		 */
		DEBUG_TRACE("%p: Create the 'from' interface heirarchy list\n", nci);
		from_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(from_list, ip_dest_addr, ip_src_addr, IPPROTO_UDP, in_dev, is_routed, in_dev, src_node_addr, dest_node_addr);
		if (from_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to obtain 'from' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_from_interfaces_reset(nci, from_list, from_list_first);

		DEBUG_TRACE("%p: Create source mapping\n", nci);
		src_mi = ecm_front_end_ipv6_mapping_establish_and_ref(in_dev, ip_src_addr, src_port, from_list, from_list_first, src_node_addr);
		ecm_db_connection_interfaces_deref(from_list, from_list_first);
		if (!src_mi) {
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to establish src mapping\n");
			return NF_ACCEPT;
		}

		DEBUG_TRACE("%p: Create the 'to' interface heirarchy list\n", nci);
		to_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(to_list, ip_src_addr, ip_dest_addr, IPPROTO_UDP, out_dev, is_routed, in_dev, dest_node_addr, src_node_addr);
		if (to_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to obtain 'to' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_to_interfaces_reset(nci, to_list, to_list_first);

		DEBUG_TRACE("%p: Create dest mapping\n", nci);
		dest_mi = ecm_front_end_ipv6_mapping_establish_and_ref(out_dev, ip_dest_addr, dest_port, to_list, to_list_first, dest_node_addr);
		ecm_db_connection_interfaces_deref(to_list, to_list_first);
		if (!dest_mi) {
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to establish dest mapping\n");
			return NF_ACCEPT;
		}

		/*
		 * Connection must have a front end instance associated with it
		 */
		feci = (struct ecm_front_end_connection_instance *)ecm_front_end_ipv6_connection_udp_instance_alloc(nci, src_mi, dest_mi, can_accel);
		if (!feci) {
			ecm_db_mapping_deref(dest_mi);
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to allocate front end\n");
			return NF_ACCEPT;
		}

		/*
		 * Every connection also needs a default classifier
		 */
		dci = ecm_classifier_default_instance_alloc(nci, IPPROTO_UDP, ecm_dir, src_port, dest_port);
		if (!dci) {
			feci->deref(feci);
			ecm_db_mapping_deref(dest_mi);
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to allocate default classifier\n");
			return NF_ACCEPT;
		}

		/*
		 * Every connection starts with a full complement of classifiers assigned.
		 * NOTE: Default classifier is a special case considered previously
		 */
		for (classifier_type = ECM_CLASSIFIER_TYPE_DEFAULT + 1; classifier_type < ECM_CLASSIFIER_TYPES; ++classifier_type) {
			struct ecm_classifier_instance *aci = ecm_front_end_ipv6_assign_classifier(nci, classifier_type);
			if (aci) {
				aci->deref(aci);
			} else {
				dci->base.deref((struct ecm_classifier_instance *)dci);
				feci->deref(feci);
				ecm_db_mapping_deref(dest_mi);
				ecm_db_mapping_deref(src_mi);
				ecm_db_connection_deref(nci);
				DEBUG_WARN("Failed to allocate classifiers assignments\n");
				return NF_ACCEPT;
			}
		}

		/*
		 * Now add the connection into the database.
		 * NOTE: In an SMP situation such as ours there is a possibility that more than one packet for the same
		 * connection is being processed simultaneously.
		 * We *could* end up creating more than one connection instance for the same actual connection.
		 * To guard against this we now perform a mutex'd lookup of the connection + add once more - another cpu may have created it before us.
		 */
		spin_lock_bh(&ecm_front_end_ipv6_lock);
		ci = ecm_db_connection_find_and_ref(ip_src_addr, ip_dest_addr, IPPROTO_UDP, src_port, dest_port);
		if (ci) {
			/*
			 * Another cpu created the same connection before us - use the one we just found
			 */
			spin_unlock_bh(&ecm_front_end_ipv6_lock);
			ecm_db_connection_deref(nci);
		} else {
			struct ecm_tracker_instance *ti;
			ecm_db_timer_group_t tg;
			ecm_tracker_sender_state_t src_state;
			ecm_tracker_sender_state_t dest_state;
			ecm_tracker_connection_state_t state;

			/*
			 * Ask tracker for timer group to set the connection to initially.
			 */
			ti = dci->tracker_get_and_ref(dci);
			ti->state_get(ti, &src_state, &dest_state, &state, &tg);
			ti->deref(ti);

			/*
			 * Add the new connection we created into the database
			 * NOTE: assign to a short timer group for now - it is the assigned classifiers responsibility to do this
			 */
			ecm_db_connection_add(nci, feci, dci,
					src_mi, dest_mi, src_mi, dest_mi,
					IPPROTO_UDP, ecm_dir,
					NULL /* final callback */,
					tg, is_routed, nci);

			spin_unlock_bh(&ecm_front_end_ipv6_lock);

			ci = nci;
			DEBUG_INFO("%p: New UDP connection created\n", ci);
		}

		/*
		 * No longer need referenecs to the objects we created
		 */
		feci->deref(feci);
		dci->base.deref((struct ecm_classifier_instance *)dci);
		ecm_db_mapping_deref(src_mi);
		ecm_db_mapping_deref(dest_mi);
	}

	/*
	 * Keep connection alive as we have seen activity
	 */
	if (!ecm_db_connection_defunct_timer_touch(ci)) {
		ecm_db_connection_deref(ci);
		return NF_ACCEPT;
	}

	/*
	 * Do we need to action generation change?
	 */
	if (unlikely(ecm_db_connection_classifier_generation_changed(ci))) {
		int i;
		bool reclassify_allowed;
		int32_t to_list_first;
		struct ecm_db_iface_instance *to_list[ECM_DB_IFACE_HEIRARCHY_MAX];
		int32_t from_list_first;
		struct ecm_db_iface_instance *from_list[ECM_DB_IFACE_HEIRARCHY_MAX];

		DEBUG_INFO("%p: re-gen needed\n", ci);

		/*
		 * Update the interface lists - these may have changed, e.g. LAG path change etc.
		 * NOTE: We never have to change the usual mapping->host->node_iface arrangements for each side of the connection (to/from sides)
		 * This is because if these interfaces change then the connection is dead anyway.
		 * But a LAG slave might change the heirarchy the connection is using but the LAG master is still sane.
		 * GGG TODO The empty list checks may mean that stale interface list information remains on a connection - this could be bad.
		 * GGG Investigate the removal of the empty list checks.
		 */
		DEBUG_TRACE("%p: Update the 'from' interface heirarchy list\n", ci);
		from_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(from_list, ip_dest_addr, ip_src_addr, IPPROTO_UDP, in_dev, is_routed, in_dev, src_node_addr, dest_node_addr);
		if (from_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(ci);
			DEBUG_WARN("Failed to obtain 'from' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_from_interfaces_reset(ci, from_list, from_list_first);
		ecm_db_connection_interfaces_deref(from_list, from_list_first);

		DEBUG_TRACE("%p: Update the 'to' interface heirarchy list\n", ci);
		to_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(to_list, ip_src_addr, ip_dest_addr, IPPROTO_UDP, out_dev, is_routed, in_dev, dest_node_addr, src_node_addr);
		if (to_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(ci);
			DEBUG_WARN("Failed to obtain 'to' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_to_interfaces_reset(ci, to_list, to_list_first);
		ecm_db_connection_interfaces_deref(to_list, to_list_first);

		/*
		 * Get list of assigned classifiers to reclassify.
		 * Remember: This also includes our default classifier too.
		 */
		assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(ci, assignments);

		/*
		 * All of the assigned classifiers must permit reclassification.
		 */
		reclassify_allowed = true;
		for (i = 0; i < assignment_count; ++i) {
			DEBUG_TRACE("%p: Calling to reclassify: %p, type: %d\n", ci, assignments[i], assignments[i]->type_get(assignments[i]));
			if (!assignments[i]->reclassify_allowed(assignments[i])) {
				DEBUG_TRACE("%p: reclassify denied: %p, by type: %d\n", ci, assignments[i], assignments[i]->type_get(assignments[i]));
				reclassify_allowed = false;
				break;
			}
		}

		if (!reclassify_allowed) {
			DEBUG_WARN("%p: re-gen denied\n", ci);
		} else {
			/*
			 * Reclassify
			 */
			DEBUG_TRACE("%p: reclassify\n", ci);
			if (!ecm_front_end_ipv6_reclassify(ci, assignment_count, assignments)) {
				DEBUG_WARN("%p: Regeneration failed, dropping packet\n", ci);
				ecm_db_connection_assignments_release(assignment_count, assignments);
				ecm_db_connection_deref(ci);
				return NF_ACCEPT;
			}
			DEBUG_TRACE("%p: reclassify success\n", ci);
		}

		/*
		 * Release the assignments and re-obtain them as there may be new ones been reassigned.
		 */
		ecm_db_connection_assignments_release(assignment_count, assignments);
	}

	/*
	 * Identify which side of the connection is sending
	 */
	ecm_db_connection_from_address_get(ci, match_addr);
	if (ECM_IP_ADDR_MATCH(ip_src_addr, match_addr)) {
		sender = ECM_TRACKER_SENDER_TYPE_SRC;
	} else {
		sender = ECM_TRACKER_SENDER_TYPE_DEST;
	}

	/*
	 * Iterate the assignments and call to process!
	 * Policy implemented:
	 * 1. Classifiers that say they are not relevant are unassigned and not actioned further.
	 * 2. Any drop command from any classifier is honoured.
	 * 3. All classifiers must action acceleration for accel to be honoured, any classifiers not sure of their relevance will stop acceleration.
	 * 4. Only the highest priority classifier, that actions it, will have its qos tag honoured.
	 * 5. Only the highest priority classifier, that actions it, will have its timer group honoured.
	 */
	DEBUG_TRACE("%p: process begin, skb: %p\n", ci, skb);
	prevalent_pr.process_actions = 0;
	prevalent_pr.drop = false;
	prevalent_pr.flow_qos_tag = skb->priority;
	prevalent_pr.return_qos_tag = skb->priority;
	prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_ACCEL;
	prevalent_pr.timer_group = ci_orig_timer_group = ecm_db_connection_timer_group_get(ci);

	assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(ci, assignments);
	for (aci_index = 0; aci_index < assignment_count; ++aci_index) {
		struct ecm_classifier_process_response aci_pr;
		struct ecm_classifier_instance *aci;

		aci = assignments[aci_index];
		DEBUG_TRACE("%p: process: %p, type: %d\n", ci, aci, aci->type_get(aci));
		aci->process(aci, sender, iph, skb, &aci_pr);
		DEBUG_TRACE("%p: aci_pr: process actions: %x, became relevant: %u, relevance: %d, drop: %d, "
				"flow_qos_tag: %u, return_qos_tag: %u, accel_mode: %x, timer_group: %d\n",
				ci, aci_pr.process_actions, aci_pr.became_relevant, aci_pr.relevance, aci_pr.drop,
				aci_pr.flow_qos_tag, aci_pr.return_qos_tag, aci_pr.accel_mode, aci_pr.timer_group);

		if (aci_pr.relevance == ECM_CLASSIFIER_RELEVANCE_NO) {
			ecm_classifier_type_t aci_type;

			/*
			 * This classifier can be unassigned - PROVIDED it is not the default classifier
			 */
			aci_type = aci->type_get(aci);
			if (aci_type == ECM_CLASSIFIER_TYPE_DEFAULT) {
				continue;
			}

			DEBUG_INFO("%p: Classifier not relevant, unassign: %d", ci, aci_type);
			ecm_db_connection_classifier_unassign(ci, aci);
			continue;
		}

		/*
		 * Yes or Maybe relevant.
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DROP) {
			/*
			 * Drop command from any classifier is actioned.
			 */
			DEBUG_TRACE("%p: wants drop: %p, type: %d, skb: %p\n", ci, aci, aci->type_get(aci), skb);
			prevalent_pr.drop |= aci_pr.drop;
		}

		/*
		 * Accel mode permission
		 */
		if (aci_pr.relevance == ECM_CLASSIFIER_RELEVANCE_MAYBE) {
			/*
			 * Classifier not sure of its relevance - cannot accel yet
			 */
			DEBUG_TRACE("%p: accel denied by maybe: %p, type: %d\n", ci, aci, aci->type_get(aci));
			prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_NO;
		} else {
			if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_ACCEL_MODE) {
				if (aci_pr.accel_mode == ECM_CLASSIFIER_ACCELERATION_MODE_NO) {
					DEBUG_TRACE("%p: accel denied: %p, type: %d\n", ci, aci, aci->type_get(aci));
					prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_NO;
				}
				/* else yes or don't care about accel */
			}
		}

		/*
		 * Timer group (the last classifier i.e. the highest priority one) will 'win'
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_TIMER_GROUP) {
			DEBUG_TRACE("%p: timer group: %p, type: %d, group: %d\n", ci, aci, aci->type_get(aci), aci_pr.timer_group);
			prevalent_pr.timer_group = aci_pr.timer_group;
		}

		/*
		 * Qos tag (the last classifier i.e. the highest priority one) will 'win'
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_QOS_TAG) {
			DEBUG_TRACE("%p: aci: %p, type: %d, flow qos tag: %u, return qos tag: %u\n",
					ci, aci, aci->type_get(aci), aci_pr.flow_qos_tag, aci_pr.return_qos_tag);
			prevalent_pr.flow_qos_tag = aci_pr.flow_qos_tag;
			prevalent_pr.return_qos_tag = aci_pr.return_qos_tag;
		}

		/*
		 * If any classifier denied DSCP remarking then that overrides every classifier
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY) {
			DEBUG_TRACE("%p: aci: %p, type: %d, DSCP remark denied\n",
					ci, aci, aci->type_get(aci));
			prevalent_pr.process_actions |= ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY;
			prevalent_pr.process_actions &= ~ECM_CLASSIFIER_PROCESS_ACTION_DSCP;
		}

		/*
		 * DSCP remark action, but only if it has not been denied by any classifier
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP) {
			if (!(prevalent_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY)) {
				DEBUG_TRACE("%p: aci: %p, type: %d, DSCP remark wanted, flow_dscp: %u, return dscp: %u\n",
						ci, aci, aci->type_get(aci), aci_pr.flow_dscp, aci_pr.return_dscp);
				prevalent_pr.process_actions |= ECM_CLASSIFIER_PROCESS_ACTION_DSCP;
				prevalent_pr.flow_dscp = aci_pr.flow_dscp;
				prevalent_pr.return_dscp = aci_pr.return_dscp;
			}
		}
	}
	ecm_db_connection_assignments_release(assignment_count, assignments);

	/*
	 * Change timer group?
	 */
	if (ci_orig_timer_group != prevalent_pr.timer_group) {
		DEBUG_TRACE("%p: change timer group from: %d to: %d\n", ci, ci_orig_timer_group, prevalent_pr.timer_group);
		ecm_db_connection_defunct_timer_reset(ci, prevalent_pr.timer_group);
	}

	/*
	 * Drop?
	 */
	if (prevalent_pr.drop) {
		DEBUG_TRACE("%p: drop: %p\n", ci, skb);
		ecm_db_connection_data_totals_update_dropped(ci, (sender == ECM_TRACKER_SENDER_TYPE_SRC)? true : false, skb->len, 1);
		ecm_db_connection_deref(ci);
		return NF_ACCEPT;
	}
	ecm_db_connection_data_totals_update(ci, (sender == ECM_TRACKER_SENDER_TYPE_SRC)? true : false, skb->len, 1);

	/*
	 * Assign qos tag
	 * GGG TODO Should we use sender to identify whether to use flow or return qos tag?
	 */
	skb->priority = prevalent_pr.flow_qos_tag;
	DEBUG_TRACE("%p: skb priority: %u\n", ci, skb->priority);

	/*
	 * Accelerate?
	 */
	if (prevalent_pr.accel_mode == ECM_CLASSIFIER_ACCELERATION_MODE_ACCEL) {
		struct ecm_front_end_connection_instance *feci;
		DEBUG_TRACE("%p: accel\n", ci);
		feci = ecm_db_connection_front_end_get_and_ref(ci);
		ecm_front_end_ipv6_connection_udp_front_end_accelerate(feci, &prevalent_pr);
		feci->deref(feci);
	}
	ecm_db_connection_deref(ci);

	return NF_ACCEPT;
}

/*
 * ecm_front_end_ipv6_non_ported_process()
 *	Process a protocol that does not have port based identifiers
 */
static unsigned int ecm_front_end_ipv6_non_ported_process(struct net_device *out_dev,
							struct net_device *in_dev,
							uint8_t *src_node_addr,
							uint8_t *dest_node_addr,
							bool can_accel, bool is_routed, struct sk_buff *skb,
							struct ecm_tracker_ip_header *ip_hdr,
							struct nf_conn *ct, enum ip_conntrack_dir ct_dir, ecm_db_direction_t ecm_dir,
							struct nf_conntrack_tuple *orig_tuple, struct nf_conntrack_tuple *reply_tuple,
							ip_addr_t ip_src_addr, ip_addr_t ip_dest_addr)
{
	struct ecm_db_connection_instance *ci;
	ecm_tracker_sender_type_t sender;
	int protocol;
	int src_port;
	int dest_port;
	ip_addr_t match_addr;
	struct ecm_classifier_instance *assignments[ECM_CLASSIFIER_TYPES];
	int aci_index;
	int assignment_count;
	ecm_db_timer_group_t ci_orig_timer_group;
	struct ecm_classifier_process_response prevalent_pr;

	DEBUG_TRACE("Non-ported protocol src: " ECM_IP_ADDR_OCTAL_FMT ", dest: " ECM_IP_ADDR_OCTAL_FMT "\n",
				ECM_IP_ADDR_TO_OCTAL(ip_src_addr), ECM_IP_ADDR_TO_OCTAL(ip_dest_addr));

	/*
	 * Look up a connection.
	 */
	protocol = (int)orig_tuple->dst.protonum;
	if ((protocol == IPPROTO_IPIP)) {
		src_port = 0;
		dest_port = 0;
	} else {
		/*
		 * Do not accelerate the non-ported connections except the ones we handle.
		 */
		can_accel = false;

		/*
		 * port numbers are just the negative protocol number equivalents for now.
		 * GGG They could eventually be used as protocol specific identifiers such as icmp id's etc.
		 */
		src_port = -protocol;
		dest_port = -protocol;
	}
	ci = ecm_db_connection_find_and_ref(ip_src_addr, ip_dest_addr, protocol, src_port, dest_port);

	/*
	 * If there is no existing connection then create a new one.
	 */
	if (unlikely(!ci)) {
		struct ecm_db_mapping_instance *src_mi;
		struct ecm_db_mapping_instance *dest_mi;
		struct ecm_classifier_default_instance *dci;
		struct ecm_front_end_connection_instance *feci;
		struct ecm_db_connection_instance *nci;
		ecm_classifier_type_t classifier_type;
		int32_t to_list_first;
		struct ecm_db_iface_instance *to_list[ECM_DB_IFACE_HEIRARCHY_MAX];
		int32_t from_list_first;
		struct ecm_db_iface_instance *from_list[ECM_DB_IFACE_HEIRARCHY_MAX];

		DEBUG_INFO("New connection from " ECM_IP_ADDR_OCTAL_FMT " to " ECM_IP_ADDR_OCTAL_FMT "\n", ECM_IP_ADDR_TO_OCTAL(ip_src_addr), ECM_IP_ADDR_TO_OCTAL(ip_dest_addr));

		/*
		 * Before we attempt to create the connection are we being terminated?
		 */
		spin_lock_bh(&ecm_front_end_ipv6_lock);
		if (ecm_front_end_ipv6_terminate_pending) {
			spin_unlock_bh(&ecm_front_end_ipv6_lock);
			DEBUG_WARN("Terminating\n");

			/*
			 * As we are terminating we just allow the packet to pass - it's no longer our concern
			 */
			return NF_ACCEPT;
		}
		spin_unlock_bh(&ecm_front_end_ipv6_lock);

		/*
		 * Now allocate the new connection
		 */
		nci = ecm_db_connection_alloc();
		if (!nci) {
			DEBUG_WARN("Failed to allocate connection\n");
			return NF_ACCEPT;
		}

		/*
		 * Get the src and destination mappings
		 * For this we also need the interface lists which we also set upon the new connection while we are at it.
		 * GGG TODO rework terms of "src/dest" - these need to be named consistently as from/to as per database terms.
		 * GGG TODO The empty list checks should not be needed, mapping_establish_and_ref() should fail out if there is no list anyway.
		 */
		DEBUG_TRACE("%p: Create the 'from' interface heirarchy list\n", nci);
		from_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(from_list, ip_dest_addr, ip_src_addr, protocol, in_dev, is_routed, in_dev, src_node_addr, dest_node_addr);
		if (from_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to obtain 'from' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_from_interfaces_reset(nci, from_list, from_list_first);

		DEBUG_TRACE("%p: Create source mapping\n", nci);
		src_mi = ecm_front_end_ipv6_mapping_establish_and_ref(in_dev, ip_src_addr, src_port, from_list, from_list_first, src_node_addr);
		ecm_db_connection_interfaces_deref(from_list, from_list_first);
		if (!src_mi) {
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to establish src mapping\n");
			return NF_ACCEPT;
		}

		DEBUG_TRACE("%p: Create the 'to' interface heirarchy list\n", nci);
		to_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(to_list, ip_src_addr, ip_dest_addr, protocol, out_dev, is_routed, in_dev, dest_node_addr, src_node_addr);
		if (to_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to obtain 'to' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_to_interfaces_reset(nci, to_list, to_list_first);

		DEBUG_TRACE("%p: Create dest mapping\n", nci);
		dest_mi = ecm_front_end_ipv6_mapping_establish_and_ref(out_dev, ip_dest_addr, dest_port, to_list, to_list_first, dest_node_addr);
		ecm_db_connection_interfaces_deref(to_list, to_list_first);
		if (!dest_mi) {
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to establish dest mapping\n");
			return NF_ACCEPT;
		}

		/*
		 * Connection must have a front end instance associated with it
		 */
		feci = (struct ecm_front_end_connection_instance *)ecm_front_end_ipv6_connection_non_ported_instance_alloc(nci, src_mi, dest_mi, can_accel);
		if (!feci) {
			ecm_db_mapping_deref(dest_mi);
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to allocate front end\n");
			return NF_ACCEPT;
		}

		/*
		 * Every connection also needs a default classifier
		 */
		dci = ecm_classifier_default_instance_alloc(nci, protocol, ecm_dir, src_port, dest_port);
		if (!dci) {
			feci->deref(feci);
			ecm_db_mapping_deref(dest_mi);
			ecm_db_mapping_deref(src_mi);
			ecm_db_connection_deref(nci);
			DEBUG_WARN("Failed to allocate default classifier\n");
			return NF_ACCEPT;
		}

		/*
		 * Every connection starts with a full complement of classifiers assigned.
		 * NOTE: Default classifier is a special case considered previously
		 */
		for (classifier_type = ECM_CLASSIFIER_TYPE_DEFAULT + 1; classifier_type < ECM_CLASSIFIER_TYPES; ++classifier_type) {
			struct ecm_classifier_instance *aci = ecm_front_end_ipv6_assign_classifier(nci, classifier_type);
			if (aci) {
				aci->deref(aci);
			} else {
				dci->base.deref((struct ecm_classifier_instance *)dci);
				feci->deref(feci);
				ecm_db_mapping_deref(dest_mi);
				ecm_db_mapping_deref(src_mi);
				ecm_db_connection_deref(nci);
				DEBUG_WARN("Failed to allocate classifiers assignments\n");
				return NF_ACCEPT;
			}
		}

		/*
		 * Now add the connection into the database.
		 * NOTE: In an SMP situation such as ours there is a possibility that more than one packet for the same
		 * connection is being processed simultaneously.
		 * We *could* end up creating more than one connection instance for the same actual connection.
		 * To guard against this we now perform a mutex'd lookup of the connection + add once more - another cpu may have created it before us.
		 */
		spin_lock_bh(&ecm_front_end_ipv6_lock);
		ci = ecm_db_connection_find_and_ref(ip_src_addr, ip_dest_addr, protocol, src_port, dest_port);
		if (ci) {
			/*
			 * Another cpu created the same connection before us - use the one we just found
			 */
			spin_unlock_bh(&ecm_front_end_ipv6_lock);
			ecm_db_connection_deref(nci);
		} else {
			struct ecm_tracker_instance *ti;
			ecm_db_timer_group_t tg;
			ecm_tracker_sender_state_t src_state;
			ecm_tracker_sender_state_t dest_state;
			ecm_tracker_connection_state_t state;

			/*
			 * Ask tracker for timer group to set the connection to initially.
			 */
			ti = dci->tracker_get_and_ref(dci);
			ti->state_get(ti, &src_state, &dest_state, &state, &tg);
			ti->deref(ti);

			/*
			 * Add the new connection we created into the database
			 * NOTE: assign to a short timer group for now - it is the assigned classifiers responsibility to do this
			 */
			ecm_db_connection_add(nci, feci, dci,
					src_mi, dest_mi, src_mi, dest_mi,
					protocol, ecm_dir,
					NULL /* final callback */,
					tg, is_routed, nci);

			spin_unlock_bh(&ecm_front_end_ipv6_lock);

			ci = nci;
			DEBUG_INFO("%p: New Non-ported protocol %d connection created\n", ci, protocol);
		}

		/*
		 * No longer need referenecs to the objects we created
		 */
		feci->deref(feci);
		dci->base.deref((struct ecm_classifier_instance *)dci);
		ecm_db_mapping_deref(src_mi);
		ecm_db_mapping_deref(dest_mi);
	}

	/*
	 * Keep connection alive as we have seen activity
	 */
	if (!ecm_db_connection_defunct_timer_touch(ci)) {
		ecm_db_connection_deref(ci);
		return NF_ACCEPT;
	}

	/*
	 * Do we need to action generation change?
	 */
	if (unlikely(ecm_db_connection_classifier_generation_changed(ci))) {
		int i;
		bool reclassify_allowed;
		int32_t to_list_first;
		struct ecm_db_iface_instance *to_list[ECM_DB_IFACE_HEIRARCHY_MAX];
		int32_t from_list_first;
		struct ecm_db_iface_instance *from_list[ECM_DB_IFACE_HEIRARCHY_MAX];

		DEBUG_INFO("%p: re-gen needed\n", ci);

		/*
		 * Update the interface lists - these may have changed, e.g. LAG path change etc.
		 * NOTE: We never have to change the usual mapping->host->node_iface arrangements for each side of the connection (to/from sides)
		 * This is because if these interfaces change then the connection is dead anyway.
		 * But a LAG slave might change the heirarchy the connection is using but the LAG master is still sane.
		 * GGG TODO The empty list checks may mean that stale interface list information remains on a connection - this could be bad.
		 * GGG Investigate the removal of the empty list checks.
		 */
		DEBUG_TRACE("%p: Update the 'from' interface heirarchy list\n", ci);
		from_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(from_list, ip_dest_addr, ip_src_addr, protocol, in_dev, is_routed, in_dev, src_node_addr, dest_node_addr);
		if (from_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(ci);
			DEBUG_WARN("Failed to obtain 'from' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_from_interfaces_reset(ci, from_list, from_list_first);
		ecm_db_connection_interfaces_deref(from_list, from_list_first);

		DEBUG_TRACE("%p: Update the 'to' interface heirarchy list\n", ci);
		to_list_first = ecm_front_end_ipv6_interface_heirarchy_construct(to_list, ip_src_addr, ip_dest_addr, protocol, out_dev, is_routed, in_dev, dest_node_addr, src_node_addr);
		if (to_list_first == ECM_DB_IFACE_HEIRARCHY_MAX) {
			ecm_db_connection_deref(ci);
			DEBUG_WARN("Failed to obtain 'to' heirarchy list\n");
			return NF_ACCEPT;
		}
		ecm_db_connection_to_interfaces_reset(ci, to_list, to_list_first);
		ecm_db_connection_interfaces_deref(to_list, to_list_first);

		/*
		 * Get list of assigned classifiers to reclassify.
		 * Remember: This also includes our default classifier too.
		 */
		assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(ci, assignments);

		/*
		 * All of the assigned classifiers must permit reclassification.
		 */
		reclassify_allowed = true;
		for (i = 0; i < assignment_count; ++i) {
			DEBUG_TRACE("%p: Calling to reclassify: %p, type: %d\n", ci, assignments[i], assignments[i]->type_get(assignments[i]));
			if (!assignments[i]->reclassify_allowed(assignments[i])) {
				DEBUG_TRACE("%p: reclassify denied: %p, by type: %d\n", ci, assignments[i], assignments[i]->type_get(assignments[i]));
				reclassify_allowed = false;
				break;
			}
		}

		if (!reclassify_allowed) {
			DEBUG_WARN("%p: re-gen denied\n", ci);
		} else {
			/*
			 * Reclassify
			 */
			DEBUG_TRACE("%p: reclassify\n", ci);
			if (!ecm_front_end_ipv6_reclassify(ci, assignment_count, assignments)) {
				DEBUG_WARN("%p: Regeneration failed, dropping packet\n", ci);
				ecm_db_connection_assignments_release(assignment_count, assignments);
				ecm_db_connection_deref(ci);
				return NF_ACCEPT;
			}
			DEBUG_TRACE("%p: reclassify success\n", ci);
		}

		/*
		 * Release the assignments and re-obtain them as there may be new ones been reassigned.
		 */
		ecm_db_connection_assignments_release(assignment_count, assignments);
	}

	/*
	 * Identify which side of the connection is sending
	 */
	ecm_db_connection_from_address_get(ci, match_addr);
	if (ECM_IP_ADDR_MATCH(ip_src_addr, match_addr)) {
		sender = ECM_TRACKER_SENDER_TYPE_SRC;
	} else {
		sender = ECM_TRACKER_SENDER_TYPE_DEST;
	}

	/*
	 * Iterate the assignments and call to process!
	 * Policy implemented:
	 * 1. Classifiers that say they are not relevant are unassigned and not actioned further.
	 * 2. Any drop command from any classifier is honoured.
	 * 3. Accel is never allowed for non-ported type connections.
	 * 4. Only the highest priority classifier, that actions it, will have its qos tag honoured.
	 * 5. Only the highest priority classifier, that actions it, will have its timer group honoured.
	 */
	DEBUG_TRACE("%p: process begin, skb: %p\n", ci, skb);
	prevalent_pr.process_actions = 0;
	prevalent_pr.drop = false;
	prevalent_pr.flow_qos_tag = skb->priority;
	prevalent_pr.return_qos_tag = skb->priority;
	prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_ACCEL;
	prevalent_pr.timer_group = ci_orig_timer_group = ecm_db_connection_timer_group_get(ci);

	assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(ci, assignments);
	for (aci_index = 0; aci_index < assignment_count; ++aci_index) {
		struct ecm_classifier_process_response aci_pr;
		struct ecm_classifier_instance *aci;

		aci = assignments[aci_index];
		DEBUG_TRACE("%p: process: %p, type: %d\n", ci, aci, aci->type_get(aci));
		aci->process(aci, sender, ip_hdr, skb, &aci_pr);
		DEBUG_TRACE("%p: aci_pr: process actions: %x, became relevant: %u, relevance: %d, drop: %d, "
				"flow_qos_tag: %u, return_qos_tag: %u, accel_mode: %x, timer_group: %d\n",
				ci, aci_pr.process_actions, aci_pr.became_relevant, aci_pr.relevance, aci_pr.drop,
				aci_pr.flow_qos_tag, aci_pr.return_qos_tag, aci_pr.accel_mode, aci_pr.timer_group);

		if (aci_pr.relevance == ECM_CLASSIFIER_RELEVANCE_NO) {
			ecm_classifier_type_t aci_type;

			/*
			 * This classifier can be unassigned - PROVIDED it is not the default classifier
			 */
			aci_type = aci->type_get(aci);
			if (aci_type == ECM_CLASSIFIER_TYPE_DEFAULT) {
				continue;
			}

			DEBUG_INFO("%p: Classifier not relevant, unassign: %d", ci, aci_type);
			ecm_db_connection_classifier_unassign(ci, aci);
			continue;
		}

		/*
		 * Yes or Maybe relevant.
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DROP) {
			/*
			 * Drop command from any classifier is actioned.
			 */
			DEBUG_TRACE("%p: wants drop: %p, type: %d, skb: %p\n", ci, aci, aci->type_get(aci), skb);
			prevalent_pr.drop |= aci_pr.drop;
		}

		/*
		 * Accel mode permission
		 */
		if (aci_pr.relevance == ECM_CLASSIFIER_RELEVANCE_MAYBE) {
			/*
			 * Classifier not sure of its relevance - cannot accel yet
			 */
			DEBUG_TRACE("%p: accel denied by maybe: %p, type: %d\n", ci, aci, aci->type_get(aci));
			prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_NO;
		} else {
			if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_ACCEL_MODE) {
				if (aci_pr.accel_mode == ECM_CLASSIFIER_ACCELERATION_MODE_NO) {
					DEBUG_TRACE("%p: accel denied: %p, type: %d\n", ci, aci, aci->type_get(aci));
					prevalent_pr.accel_mode = ECM_CLASSIFIER_ACCELERATION_MODE_NO;
				}
				/* else yes or don't care about accel */
			}
		}

		/*
		 * Timer group (the last classifier i.e. the highest priority one) will 'win'
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_TIMER_GROUP) {
			DEBUG_TRACE("%p: timer group: %p, type: %d, group: %d\n", ci, aci, aci->type_get(aci), aci_pr.timer_group);
			prevalent_pr.timer_group = aci_pr.timer_group;
		}

		/*
		 * Qos tag (the last classifier i.e. the highest priority one) will 'win'
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_QOS_TAG) {
			DEBUG_TRACE("%p: aci: %p, type: %d, flow qos tag: %u, return qos tag: %u\n",
					ci, aci, aci->type_get(aci), aci_pr.flow_qos_tag, aci_pr.return_qos_tag);
			prevalent_pr.flow_qos_tag = aci_pr.flow_qos_tag;
			prevalent_pr.return_qos_tag = aci_pr.return_qos_tag;
		}

		/*
		 * If any classifier denied DSCP remarking then that overrides every classifier
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY) {
			DEBUG_TRACE("%p: aci: %p, type: %d, DSCP remark denied\n",
					ci, aci, aci->type_get(aci));
			prevalent_pr.process_actions |= ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY;
			prevalent_pr.process_actions &= ~ECM_CLASSIFIER_PROCESS_ACTION_DSCP;
		}

		/*
		 * DSCP remark action, but only if it has not been denied by any classifier
		 */
		if (aci_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP) {
			if (!(prevalent_pr.process_actions & ECM_CLASSIFIER_PROCESS_ACTION_DSCP_DENY)) {
				DEBUG_TRACE("%p: aci: %p, type: %d, DSCP remark wanted, flow_dscp: %u, return dscp: %u\n",
						ci, aci, aci->type_get(aci), aci_pr.flow_dscp, aci_pr.return_dscp);
				prevalent_pr.process_actions |= ECM_CLASSIFIER_PROCESS_ACTION_DSCP;
				prevalent_pr.flow_dscp = aci_pr.flow_dscp;
				prevalent_pr.return_dscp = aci_pr.return_dscp;
			}
		}
	}
	ecm_db_connection_assignments_release(assignment_count, assignments);

	/*
	 * Change timer group?
	 */
	if (ci_orig_timer_group != prevalent_pr.timer_group) {
		DEBUG_TRACE("%p: change timer group from: %d to: %d\n", ci, ci_orig_timer_group, prevalent_pr.timer_group);
		ecm_db_connection_defunct_timer_reset(ci, prevalent_pr.timer_group);
	}

	/*
	 * Drop?
	 */
	if (prevalent_pr.drop) {
		DEBUG_TRACE("%p: drop: %p\n", ci, skb);
		ecm_db_connection_data_totals_update_dropped(ci, (sender == ECM_TRACKER_SENDER_TYPE_SRC)? true : false, skb->len, 1);
		ecm_db_connection_deref(ci);
		return NF_ACCEPT;
	}
	ecm_db_connection_data_totals_update(ci, (sender == ECM_TRACKER_SENDER_TYPE_SRC)? true : false, skb->len, 1);

	/*
	 * Assign qos tag
	 * GGG TODO Should we use sender to identify whether to use flow or return qos tag?
	 */
	skb->priority = prevalent_pr.flow_qos_tag;
	DEBUG_TRACE("%p: skb priority: %u\n", ci, skb->priority);

	/*
	 * Accelerate?
	 */
	if (prevalent_pr.accel_mode == ECM_CLASSIFIER_ACCELERATION_MODE_ACCEL) {
		struct ecm_front_end_connection_instance *feci;
		DEBUG_TRACE("%p: accel\n", ci);
		feci = ecm_db_connection_front_end_get_and_ref(ci);
		ecm_front_end_ipv6_connection_non_ported_front_end_accelerate(feci, &prevalent_pr);
		feci->deref(feci);
	}
	ecm_db_connection_deref(ci);

	return NF_ACCEPT;
}

/*
 * ecm_front_end_ipv6_ip_process()
 *	Process IP datagram skb
 */
static unsigned int ecm_front_end_ipv6_ip_process(struct net_device *out_dev, struct net_device *in_dev,
							uint8_t *src_node_addr, uint8_t *dest_node_addr,
							bool can_accel, bool is_routed, struct sk_buff *skb)
{
	struct ecm_tracker_ip_header ip_hdr;
        struct nf_conn *ct;
        enum ip_conntrack_info ctinfo;
	struct nf_conntrack_tuple orig_tuple;
	struct nf_conntrack_tuple reply_tuple;
	enum ip_conntrack_dir ct_dir;
	ecm_db_direction_t ecm_dir = ECM_DB_DIRECTION_EGRESS_NAT;
	ip_addr_t ip_src_addr;
	ip_addr_t ip_dest_addr;

	/*
	 * Obtain the IP header from the skb
	 */
	if (!ecm_tracker_ip_check_header_and_read(&ip_hdr, skb)) {
		DEBUG_WARN("Invalid ip header in skb %p\n", skb);
		return NF_ACCEPT;
	}

	if (ip_hdr.fragmented) {
		DEBUG_TRACE("skb %p is fragmented\n", skb);
		return NF_ACCEPT;
	}

	/*
	 * Extract information, if we have conntrack then use that info as far as we can.
	 */
        ct = nf_ct_get(skb, &ctinfo);
	if (unlikely(!ct)) {
		DEBUG_TRACE("%p: no ct\n", skb);
		/*
		 * TODO: ECM_IP_ADDR_COPY() can be used to copy an array with size of four 4-bytes integers.
		 */
		ECM_IP_ADDR_COPY(orig_tuple.src.u3.in6.in6_u.u6_addr32, ip_hdr.h.v6_hdr.saddr.in6_u.u6_addr32);
		ECM_IP_ADDR_COPY(orig_tuple.dst.u3.in6.in6_u.u6_addr32, ip_hdr.h.v6_hdr.daddr.in6_u.u6_addr32);
		orig_tuple.dst.protonum = ip_hdr.protocol;
		ECM_IP_ADDR_COPY(reply_tuple.src.u3.in6.in6_u.u6_addr32, orig_tuple.dst.u3.in6.in6_u.u6_addr32);
		ECM_IP_ADDR_COPY(reply_tuple.dst.u3.in6.in6_u.u6_addr32, orig_tuple.src.u3.in6.in6_u.u6_addr32);
		ct_dir = IP_CT_DIR_ORIGINAL;
	} else {
		if (unlikely(ct == &nf_conntrack_untracked)) {
			DEBUG_TRACE("%p: ct: untracked\n", skb);
			return NF_ACCEPT;
		}

		/*
		 * If the conntrack connection is using a helper (i.e. Application Layer Gateway)
		 * then acceleration is denied (connection needs assistance from HLOS to function)
		 */
		if (nfct_help(ct)) {
			DEBUG_TRACE("%p: Connection has helper\n", ct);
			can_accel = false;
		}

		/*
		 * Extract conntrack connection information
		 */
		DEBUG_TRACE("%p: ct: %p, ctinfo: %x\n", skb, ct, ctinfo);
		orig_tuple = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
		reply_tuple = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
		ct_dir = CTINFO2DIR(ctinfo);

		/*
		 * Is this a related connection?
		 */
		if ((ctinfo == IP_CT_RELATED) || (ctinfo == IP_CT_RELATED_REPLY)) {
			/*
			 * ct is related to the packet at hand.
			 * We can use the IP src/dest information and the direction information.
			 * We cannot use the protocol information from the ct (typically the packet at hand is ICMP error that is related to the ct we have here).
			 */
			orig_tuple.dst.protonum = ip_hdr.protocol;
			DEBUG_TRACE("%p: related ct, actual protocol: %u\n", skb, orig_tuple.dst.protonum);
		}
	}

	/*
	 * Work out if this packet involves routing or not.
	 */
	if (is_routed) {
		/*
		 * Non-NAT only supported for IPv6
		 */
		ecm_dir = ECM_DB_DIRECTION_NON_NAT;
	} else {
		/*
		 * Bridged
		 */
		ecm_dir = ECM_DB_DIRECTION_BRIDGED;
	}

	if (ct_dir == IP_CT_DIR_ORIGINAL) {
		if (ecm_dir == ECM_DB_DIRECTION_NON_NAT) {
			ECM_NIN6_ADDR_TO_IP_ADDR(ip_src_addr, orig_tuple.src.u3.in6);
			ECM_NIN6_ADDR_TO_IP_ADDR(ip_dest_addr, orig_tuple.dst.u3.in6);

			src_node_addr = NULL;
		} else if (ecm_dir == ECM_DB_DIRECTION_BRIDGED) {
			ECM_NIN6_ADDR_TO_IP_ADDR(ip_src_addr, orig_tuple.src.u3.in6);
			ECM_NIN6_ADDR_TO_IP_ADDR(ip_dest_addr, orig_tuple.dst.u3.in6);
		} else {
			DEBUG_ASSERT(false, "Unhandled ecm_dir: %d\n", ecm_dir);
		}
	} else {
		if (ecm_dir == ECM_DB_DIRECTION_NON_NAT) {
			ECM_NIN6_ADDR_TO_IP_ADDR(ip_dest_addr, orig_tuple.src.u3.in6);
			ECM_NIN6_ADDR_TO_IP_ADDR(ip_src_addr, orig_tuple.dst.u3.in6);

			src_node_addr = NULL;
		} else if (ecm_dir == ECM_DB_DIRECTION_BRIDGED) {
			ECM_NIN6_ADDR_TO_IP_ADDR(ip_dest_addr, orig_tuple.src.u3.in6);
			ECM_NIN6_ADDR_TO_IP_ADDR(ip_src_addr, orig_tuple.dst.u3.in6);
		} else {
			DEBUG_ASSERT(false, "Unhandled ecm_dir: %d\n", ecm_dir);
		}
	}

	DEBUG_TRACE("IP Packet src: " ECM_IP_ADDR_OCTAL_FMT "dst: " ECM_IP_ADDR_OCTAL_FMT " protocol: %u, ct_dir: %d ecm_dir: %d\n",
				ECM_IP_ADDR_TO_OCTAL(ip_src_addr),
				ECM_IP_ADDR_TO_OCTAL(ip_dest_addr),
				orig_tuple.dst.protonum, ct_dir, ecm_dir);

	/*
	 * Non-unicast source or destination packets are ignored
	 * NOTE: Only need to check the non-nat src/dest addresses here.
	 */
	if (unlikely(ecm_ip_addr_is_non_unicast(ip_dest_addr))) {
		DEBUG_TRACE("skb %p non-unicast daddr " ECM_IP_ADDR_OCTAL_FMT "\n", skb, ECM_IP_ADDR_TO_OCTAL(ip_dest_addr));
		return NF_ACCEPT;
	}
	if (unlikely(ecm_ip_addr_is_non_unicast(ip_src_addr))) {
		DEBUG_TRACE("skb %p non-unicast saddr " ECM_IP_ADDR_OCTAL_FMT "\n", skb, ECM_IP_ADDR_TO_OCTAL(ip_src_addr));
		return NF_ACCEPT;
	}

	/*
	 * Process IP specific protocol
	 * TCP and UDP are the most likliest protocols.
	 */
	if (likely(orig_tuple.dst.protonum == IPPROTO_TCP)) {
		return ecm_front_end_ipv6_tcp_process(out_dev, in_dev,
				src_node_addr,
				dest_node_addr,
				can_accel, is_routed, skb,
				&ip_hdr,
				ct, ct_dir, ecm_dir,
				&orig_tuple, &reply_tuple,
				ip_src_addr, ip_dest_addr);
	} else if (likely(orig_tuple.dst.protonum == IPPROTO_UDP)) {
		return ecm_front_end_ipv6_udp_process(out_dev, in_dev,
				src_node_addr,
				dest_node_addr,
				can_accel, is_routed, skb,
				&ip_hdr,
				ct, ct_dir, ecm_dir,
				&orig_tuple, &reply_tuple,
				ip_src_addr, ip_dest_addr);
	}
	return ecm_front_end_ipv6_non_ported_process(out_dev, in_dev,
				src_node_addr,
				dest_node_addr,
				can_accel, is_routed, skb,
				&ip_hdr,
				ct, ct_dir, ecm_dir,
				&orig_tuple, &reply_tuple,
				ip_src_addr, ip_dest_addr);
}

/*
 * ecm_front_end_ipv6_post_routing_hook()
 *	Called for IP packets that are going out to interfaces after IP routing stage.
 */
static unsigned int ecm_front_end_ipv6_post_routing_hook(unsigned int hooknum,
				struct sk_buff *skb,
				const struct net_device *in_unused,
				const struct net_device *out,
				int (*okfn)(struct sk_buff *))
{
	struct net_device *in;
	bool can_accel = true;
	unsigned int result;

	DEBUG_TRACE("%p: Routing: %s\n", out, out->name);

	/*
	 * If operations have stopped then do not process packets
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	if (unlikely(ecm_front_end_ipv6_stopped)) {
		spin_unlock_bh(&ecm_front_end_ipv6_lock);
		DEBUG_TRACE("Front end stopped\n");
		return NF_ACCEPT;
	}
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Identify interface from where this packet came
	 */
	in = dev_get_by_index(&init_net, skb->skb_iif);
	if (unlikely(!in)) {
		/*
		 * Locally sourced packets are not processed in ECM.
		 */
		return NF_ACCEPT;
	}

	DEBUG_TRACE("Post routing process skb %p, out: %p, in: %p\n", skb, out, in);
	result = ecm_front_end_ipv6_ip_process((struct net_device *)out, in, NULL, NULL, can_accel, true, skb);
	dev_put(in);
	return result;
}

/*
 * ecm_front_end_ipv6_bridge_post_routing_hook()
 *	Called for packets that are going out to one of the bridge physical interfaces.
 *
 * These may have come from another bridged interface or from a non-bridged interface.
 * Conntrack information may be available or not if this skb is bridged.
 */
static unsigned int ecm_front_end_ipv6_bridge_post_routing_hook(unsigned int hooknum,
					struct sk_buff *skb,
					const struct net_device *in_unused,
					const struct net_device *out,
					int (*okfn)(struct sk_buff *))
{
	struct ethhdr *skb_eth_hdr;
	uint16_t eth_type;
	struct net_device *bridge;
	struct net_device *in;
	bool can_accel = true;
	unsigned int result;

	DEBUG_TRACE("%p: Bridge: %s\n", out, out->name);

	/*
	 * If operations have stopped then do not process packets
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	if (unlikely(ecm_front_end_ipv6_stopped)) {
		spin_unlock_bh(&ecm_front_end_ipv6_lock);
		DEBUG_TRACE("Front end stopped\n");
		return NF_ACCEPT;
	}
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Check packet is an IP Ethernet packet
	 */
	skb_eth_hdr = eth_hdr(skb);
	if (!skb_eth_hdr) {
		DEBUG_TRACE("%p: Not Eth\n", skb);
		return NF_ACCEPT;
	}
	eth_type = ntohs(skb_eth_hdr->h_proto);
	if (unlikely(eth_type != 0x86DD)) {
		DEBUG_TRACE("%p: Not IP\n", skb);
		return NF_ACCEPT;
	}

	/*
	 * Identify interface from where this packet came.
	 * There are three scenarios to consider here:
	 * 1. Packet came from a local source.
	 *	Ignore - local is not handled.
	 * 2. Packet came from a routed path.
	 *	Ignore - it was handled in INET post routing.
	 * 3. Packet is bridged from another port.
	 *	Process.
	 *
	 * Begin by identifying case 1.
	 * NOTE: We are given 'out' (which we implicitly know is a bridge port) so out->master is the 'bridge'.
	 */
	bridge = out->master;
	in = dev_get_by_index(&init_net, skb->skb_iif);
	if  (!in) {
		/*
		 * Case 1.
		 */
		DEBUG_TRACE("Local traffic: %p, ignoring traffic to bridge: %p (%s) \n", skb, bridge, bridge->name);
		return NF_ACCEPT;
	}
	dev_put(in);

	/*
	 * Case 2:
	 *	For routed packets the skb will have the src mac matching the bridge mac.
	 * Case 3:
	 *	If the packet was not local (case 1) or routed (case 2) then we process.
	 */
	in = br_port_dev_get(out->master, skb_eth_hdr->h_source);
	if (!in) {
		DEBUG_TRACE("skb: %p, no in device for bridge: %p (%s)\n", skb, bridge, bridge->name);
		return NF_ACCEPT;
	}
	if (in == out) {
		DEBUG_TRACE("skb: %p, bridge: %p (%s), port bounce on %p (%s)\n", skb, bridge, bridge->name, out, out->name);
		dev_put(in);
		return NF_ACCEPT;
	}
	if (!compare_ether_addr(skb_eth_hdr->h_source, bridge->dev_addr)) {
		/*
		 * Case 2: Routed trafffic would be handled by the INET post routing.
		 */
		DEBUG_TRACE("skb: %p, Ignoring routed packet to bridge: %p (%s)\n", skb, bridge, bridge->name);
		dev_put(in);
		return NF_ACCEPT;
	}

	DEBUG_TRACE("Bridge process skb: %p, bridge: %p (%s), In: %p (%s), Out: %p (%s)\n",
			skb, bridge, bridge->name, in, in->name, out, out->name);
	result = ecm_front_end_ipv6_ip_process((struct net_device *)out, in,
							skb_eth_hdr->h_source, skb_eth_hdr->h_dest, can_accel, false, skb);
	dev_put(in);
	return result;
}

/*
 * ecm_front_end_ipv6_ipv6_neigh_get()
 * 	Returns neighbour reference for a given IP address which must be released when you are done with it.
 *
 * Returns NULL on fail.
 */
static struct neighbour *ecm_front_end_ipv6_ipv6_neigh_get(ip_addr_t addr)
{
	struct neighbour *neigh;
	struct rt6_info *rt;
	struct dst_entry *dst;
	struct in6_addr ipv6_addr;

	ECM_IP_ADDR_TO_NIN6_ADDR(ipv6_addr, addr);
	rt = rt6_lookup(&init_net, &ipv6_addr, NULL, 0, 0);
	if (!rt) {
		return NULL;
	}
	dst = (struct dst_entry *)rt;
	neigh = dst_neigh_lookup(dst, &ipv6_addr);
	dst_release(dst);
	return neigh;
}

/*
 * ecm_front_end_ipv6_net_dev_callback()
 *	Callback handler from the NSS.
 */
static void ecm_front_end_ipv6_net_dev_callback(void *app_data, struct nss_ipv6_msg *nim)
{
	struct nss_ipv6_conn_sync *sync;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct nf_conn_counter *acct;
	struct ecm_db_connection_instance *ci;
	struct ecm_front_end_connection_instance *feci;
	struct neighbour *neigh;
	struct ecm_classifier_instance *assignments[ECM_CLASSIFIER_TYPES];
	int aci_index;
	int assignment_count;
	ip_addr_t flow_ip;
	ip_addr_t return_ip;

	/*
	 * Only respond to sync messages
	 */
	if (nim->cm.type != NSS_IPV6_RX_CONN_STATS_SYNC_MSG) {
		DEBUG_TRACE("Ignoring nim: %p - not sync: %d", nim, nim->cm.type);
		return;
	}
	sync = &nim->msg.conn_stats;

	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(flow_ip, sync->flow_ip);
	ECM_NSS_IPV6_ADDR_TO_IP_ADDR(return_ip, sync->return_ip);

	/*
	 * Look up ecm connection with a view to synchronising the connection, classifier and data tracker.
	 * Note that we use _xlate versions for destination - for egressing connections this would be the wan IP address,
	 * but for ingressing this would be the LAN side (non-nat'ed) address and is what we need for lookup of our connection.
	 */
	DEBUG_INFO("%p: NSS Sync, lookup connection using\n" \
			"Protocol: %d\n" \
			"src_addr: " ECM_IP_ADDR_OCTAL_FMT ":%d\n" \
			"dest_addr: " ECM_IP_ADDR_OCTAL_FMT ":%d\n",
			sync,
			(int)sync->protocol,
			ECM_IP_ADDR_TO_OCTAL(flow_ip), (int)sync->flow_ident,
			ECM_IP_ADDR_TO_OCTAL(return_ip), (int)sync->return_ident);

	ci = ecm_db_connection_find_and_ref(flow_ip, return_ip, sync->protocol, (int)sync->flow_ident, (int)sync->return_ident);
	if (!ci) {
		DEBUG_TRACE("%p: NSS Sync: no connection\n", sync);
		goto sync_conntrack;
	}
	DEBUG_TRACE("%p: Sync conn %p\n", sync, ci);

	/*
	 * Keep connection alive and updated
	 */
	if (!ecm_db_connection_defunct_timer_touch(ci)) {
		ecm_db_connection_deref(ci);
		goto sync_conntrack;
	}

	/*
	 * Get the front end instance
	 */
	feci = ecm_db_connection_front_end_get_and_ref(ci);

	if (sync->flow_tx_packet_count || sync->return_tx_packet_count) {
		DEBUG_TRACE("%p: flow_tx_packet_count: %u, flow_tx_byte_count: %u, return_tx_packet_count: %u, , return_tx_byte_count: %u\n",
				ci, sync->flow_tx_packet_count, sync->flow_tx_byte_count, sync->return_tx_packet_count, sync->return_tx_byte_count);

		/*
		 * The amount of data *sent* by the ECM connection 'from' side is the amount the NSS has *received* in the 'flow' direction.
		 */
		ecm_db_connection_data_totals_update(ci, true, sync->flow_rx_byte_count, sync->flow_rx_packet_count);

		/*
		 * The amount of data *sent* by the ECM connection 'to' side is the amount the NSS has *received* in the 'return' direction.
		 */
		ecm_db_connection_data_totals_update(ci, false, sync->return_rx_byte_count, sync->return_rx_packet_count);

		/*
		 * As packets have been accelerated we have seen some action.
		 */
		feci->action_seen(feci);

		/*
		 * Update interface statistics
		 */
		ecm_interface_stats_update(ci, sync->flow_tx_packet_count, sync->flow_tx_byte_count, sync->flow_rx_packet_count, sync->flow_rx_byte_count,
						sync->return_tx_packet_count, sync->return_tx_byte_count, sync->return_rx_packet_count, sync->return_rx_byte_count);
	}

	/*
	 * Sync assigned classifiers
	 */
	assignment_count = ecm_db_connection_classifier_assignments_get_and_ref(ci, assignments);
	for (aci_index = 0; aci_index < assignment_count; ++aci_index) {
		struct ecm_classifier_instance *aci;

		aci = assignments[aci_index];
		DEBUG_TRACE("%p: sync to: %p, type: %d\n", feci, aci, aci->type_get(aci));
		aci->sync_to_v6(aci, sync);
	}
	ecm_db_connection_assignments_release(assignment_count, assignments);

	switch(sync->reason) {
	case NSS_IPV6_SYNC_REASON_DESTROY:
		/*
		 * This is the final sync from the NSS for a connection whose acceleration was
		 * terminated by the ecm.
		 * NOTE: We take no action here since that is performed by the destroy message ack.
		 */
		DEBUG_INFO("%p: ECM initiated final sync seen: %d\n", ci, sync->reason);
		break;
	case NSS_IPV6_SYNC_REASON_FLUSH:
	case NSS_IPV6_SYNC_REASON_EVICT:
		/*
		 * NSS has ended acceleration without instruction from the ECM.
		 */
		DEBUG_INFO("%p: NSS Initiated final sync seen: %d\n", ci, sync->reason);

		/*
		 * NSS Decelerated the connection
		 */
		feci->accel_ceased(feci);
		break;
	default:

		/*
		 * Update the neighbour entry for source IP address
		 */
		neigh = ecm_front_end_ipv6_ipv6_neigh_get(flow_ip);
		if (!neigh) {
			DEBUG_WARN("Neighbour entry for " ECM_IP_ADDR_OCTAL_FMT " not found\n", ECM_IP_ADDR_TO_OCTAL(flow_ip));
		} else {
			DEBUG_TRACE("Neighbour entry for " ECM_IP_ADDR_OCTAL_FMT " update: %p\n", ECM_IP_ADDR_TO_OCTAL(flow_ip), neigh);
			neigh_update(neigh, NULL, neigh->nud_state, NEIGH_UPDATE_F_WEAK_OVERRIDE);
			neigh_release(neigh);
		}

		/*
		 * Update the neighbour entry for destination IP address
		 */
		neigh = ecm_front_end_ipv6_ipv6_neigh_get(return_ip);
		if (!neigh) {
			DEBUG_WARN("Neighbour entry for " ECM_IP_ADDR_OCTAL_FMT " not found\n", ECM_IP_ADDR_TO_OCTAL(return_ip));
		} else {
			DEBUG_TRACE("Neighbour entry for " ECM_IP_ADDR_OCTAL_FMT " update: %p\n", ECM_IP_ADDR_TO_OCTAL(return_ip), neigh);
			neigh_update(neigh, NULL, neigh->nud_state, NEIGH_UPDATE_F_WEAK_OVERRIDE);
			neigh_release(neigh);
		}
	}

	/*
	 * If connection should be re-generated then we need to force a deceleration
	 */
	if (unlikely(ecm_db_connection_classifier_peek_generation_changed(ci))) {
		DEBUG_TRACE("%p: Connection generation changing, terminating acceleration", ci);
		feci->decelerate(feci);
	}

	feci->deref(feci);
	ecm_db_connection_deref(ci);

sync_conntrack:
	;

	/*
	 * Create a tuple so as to be able to look up a conntrack connection
	 */
	memset(&tuple, 0, sizeof(tuple));
	ECM_IP_ADDR_TO_NIN6_ADDR(tuple.src.u3.in6, flow_ip)
	tuple.src.u.all = (__be16)htons(sync->flow_ident);
	tuple.src.l3num = AF_INET6;

	ECM_IP_ADDR_TO_NIN6_ADDR(tuple.dst.u3.in6, return_ip);
	tuple.dst.dir = IP_CT_DIR_ORIGINAL;
	tuple.dst.protonum = (uint8_t)sync->protocol;
	tuple.dst.u.all = (__be16)htons(sync->return_ident);

	DEBUG_TRACE("Conntrack sync, lookup conntrack connection using\n"
			"Protocol: %d\n"
			"src_addr: " ECM_IP_ADDR_OCTAL_FMT ":%d\n"
			"dest_addr: " ECM_IP_ADDR_OCTAL_FMT ":%d\n",
			(int)tuple.dst.protonum,
			ECM_IP_ADDR_TO_OCTAL(flow_ip), (int)tuple.src.u.all,
			ECM_IP_ADDR_TO_OCTAL(return_ip), (int)tuple.dst.u.all);

	/*
	 * Look up conntrack connection
	 */
	h = nf_conntrack_find_get(&init_net, NF_CT_DEFAULT_ZONE, &tuple);
	if (!h) {
		DEBUG_WARN("%p: NSS Sync: no conntrack connection\n", sync);
		return;
	}

	ct = nf_ct_tuplehash_to_ctrack(h);
	NF_CT_ASSERT(ct->timeout.data == (unsigned long)ct);
	DEBUG_TRACE("%p: NSS Sync: conntrack connection\n", ct);

	/*
	 * Only update if this is not a fixed timeout
	 */
	if (!test_bit(IPS_FIXED_TIMEOUT_BIT, &ct->status)) {
		unsigned long int delta_jiffies;

		/*
		 * Convert ms ticks from the NSS to jiffies.  We know that inc_ticks is small
		 * and we expect HZ to be small too so we can multiply without worrying about
		 * wrap-around problems.  We add a rounding constant to ensure that the different
		 * time bases don't cause truncation errors.
		 */
		DEBUG_ASSERT(HZ <= 100000, "Bad HZ\n");
		delta_jiffies = ((sync->inc_ticks * HZ) + (MSEC_PER_SEC / 2)) / MSEC_PER_SEC;

		spin_lock_bh(&ct->lock);
		ct->timeout.expires += delta_jiffies;
		spin_unlock_bh(&ct->lock);
	}

	acct = nf_conn_acct_find(ct);
	if (acct) {
		spin_lock_bh(&ct->lock);
		atomic64_add(sync->flow_rx_packet_count, &acct[IP_CT_DIR_ORIGINAL].packets);
		atomic64_add(sync->flow_rx_byte_count, &acct[IP_CT_DIR_ORIGINAL].bytes);

		atomic64_add(sync->return_rx_packet_count, &acct[IP_CT_DIR_REPLY].packets);
		atomic64_add(sync->return_rx_byte_count, &acct[IP_CT_DIR_REPLY].bytes);
		spin_unlock_bh(&ct->lock);
	}

	switch (sync->protocol) {
	case IPPROTO_TCP:
		spin_lock_bh(&ct->lock);
		if (ct->proto.tcp.seen[0].td_maxwin < sync->flow_max_window) {
			ct->proto.tcp.seen[0].td_maxwin = sync->flow_max_window;
		}
		if ((int32_t)(ct->proto.tcp.seen[0].td_end - sync->flow_end) < 0) {
			ct->proto.tcp.seen[0].td_end = sync->flow_end;
		}
		if ((int32_t)(ct->proto.tcp.seen[0].td_maxend - sync->flow_max_end) < 0) {
			ct->proto.tcp.seen[0].td_maxend = sync->flow_max_end;
		}
		if (ct->proto.tcp.seen[1].td_maxwin < sync->return_max_window) {
			ct->proto.tcp.seen[1].td_maxwin = sync->return_max_window;
		}
		if ((int32_t)(ct->proto.tcp.seen[1].td_end - sync->return_end) < 0) {
			ct->proto.tcp.seen[1].td_end = sync->return_end;
		}
		if ((int32_t)(ct->proto.tcp.seen[1].td_maxend - sync->return_max_end) < 0) {
			ct->proto.tcp.seen[1].td_maxend = sync->return_max_end;
		}
		spin_unlock_bh(&ct->lock);
		break;
	}

	/*
	 * Release connection
	 */
	nf_ct_put(ct);
}

/*
 * struct nf_hook_ops ecm_front_end_ipv6_netfilter_hooks[]
 *	Hooks into netfilter packet monitoring points.
 */
static struct nf_hook_ops ecm_front_end_ipv6_netfilter_hooks[] __read_mostly = {
	/*
	 * Post routing hook is used to monitor packets going to interfaces that are NOT bridged in some way, e.g. packets to the WAN.
	 */
	{
		.hook           = ecm_front_end_ipv6_post_routing_hook,
		.owner          = THIS_MODULE,
		.pf             = PF_INET6,
		.hooknum        = NF_INET_POST_ROUTING,
		.priority       = NF_IP6_PRI_NAT_SRC + 1,
	},

	/*
	 * The bridge post routing hook monitors packets going to interfaces that are part of a bridge arrangement.
	 * For example Wireles LAN (WLAN) and Wired LAN (LAN).
	 */
	{
		.hook		= ecm_front_end_ipv6_bridge_post_routing_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_POST_ROUTING,
		.priority	= NF_BR_PRI_FILTER_OTHER,
	},
};

/*
 * ecm_front_end_ipv6_connection_from_ct_get_and_ref()
 *	Return, if any, a connection given a ct
 */
static struct ecm_db_connection_instance *ecm_front_end_ipv6_connection_from_ct_get_and_ref(struct nf_conn *ct)
{
	struct nf_conntrack_tuple orig_tuple;
	struct nf_conntrack_tuple reply_tuple;
	ip_addr_t host1_addr;
	ip_addr_t host2_addr;
	int host1_port;
	int host2_port;
	int protocol;

	/*
	 * Look up the associated connection for this conntrack connection
	 */
	orig_tuple = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	reply_tuple = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
	ECM_NIN6_ADDR_TO_IP_ADDR(host1_addr, orig_tuple.src.u3.in6);
	ECM_NIN6_ADDR_TO_IP_ADDR(host2_addr, reply_tuple.src.u3.in6);
	protocol = orig_tuple.dst.protonum;
	if (protocol == IPPROTO_TCP) {
		host1_port = ntohs(orig_tuple.src.u.tcp.port);
		host2_port = ntohs(reply_tuple.src.u.tcp.port);
	} else if (protocol == IPPROTO_UDP) {
		host1_port = ntohs(orig_tuple.src.u.udp.port);
		host2_port = ntohs(reply_tuple.src.u.udp.port);
	} else if ((protocol == IPPROTO_IPIP)) {
		host1_port = 0;
		host2_port = 0;
	} else {
		host1_port = -protocol;
		host2_port = -protocol;
	}

	DEBUG_TRACE("%p: lookup src: " ECM_IP_ADDR_OCTAL_FMT ":%d, "
		    "dest: " ECM_IP_ADDR_OCTAL_FMT ":%d, "
		    "protocol %d\n",
		    ct,
		    ECM_IP_ADDR_TO_OCTAL(host1_addr),
		    host1_port,
		    ECM_IP_ADDR_TO_OCTAL(host2_addr),
		    host2_port,
		    protocol);

	return ecm_db_connection_find_and_ref(host1_addr,
					      host2_addr,
					      protocol,
					      host1_port,
					      host2_port);
}

/*
 * ecm_front_end_ipv6_conntrack_event_destroy()
 *	Handles conntrack destroy events
 */
static void ecm_front_end_ipv6_conntrack_event_destroy(struct nf_conn *ct)
{
	struct ecm_db_connection_instance *ci;
	struct ecm_front_end_connection_instance *feci;

	DEBUG_INFO("Destroy event for ct: %p\n", ct);

	ci = ecm_front_end_ipv6_connection_from_ct_get_and_ref(ct);
	if (!ci) {
		DEBUG_TRACE("%p: not found\n", ct);
		return;
	}
	DEBUG_INFO("%p: Connection defunct %p\n", ct, ci);

	/*
	 * If this connection is accelerated then we need to issue a destroy command
	 */
	feci = ecm_db_connection_front_end_get_and_ref(ci);
	feci->decelerate(feci);
	feci->deref(feci);

	/*
	 * Force destruction of the connection my making it defunct
	 */
	ecm_db_connection_make_defunct(ci);
	ecm_db_connection_deref(ci);
}

/*
 * ecm_front_end_ipv6_conntrack_event_mark()
 *	Handles conntrack mark events
 */
static void ecm_front_end_ipv6_conntrack_event_mark(struct nf_conn *ct)
{
	struct ecm_db_connection_instance *ci;
	struct ecm_classifier_instance *cls;

	DEBUG_INFO("Mark event for ct: %p\n", ct);

	/*
	 * Ignore transitions to zero
	 */
	if (ct->mark == 0) {
		return;
	}

	ci = ecm_front_end_ipv6_connection_from_ct_get_and_ref(ct);
	if (!ci) {
		DEBUG_TRACE("%p: not found\n", ct);
		return;
	}

	/*
	 * As of now, only the Netlink classifier is interested in conmark changes
	 * GGG TODO Add a classifier method to propagate this information to any and all types of classifier.
	 */
	cls = ecm_db_connection_assigned_classifier_find_and_ref(ci, ECM_CLASSIFIER_TYPE_NL);
	if (cls) {
		ecm_classifier_nl_process_mark((struct ecm_classifier_nl_instance *)cls, ct->mark);
		cls->deref(cls);
	}

	/*
	 * All done
	 */
	ecm_db_connection_deref(ci);
}

/*
 * ecm_front_end_ipv6_conntrack_event()
 *	Callback event invoked when conntrack connection state changes, currently we handle destroy events to quickly release state
 */
int ecm_front_end_ipv6_conntrack_event(unsigned long events, struct nf_conn *ct)
{
	/*
	 * If operations have stopped then do not process event
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	if (unlikely(ecm_front_end_ipv6_stopped)) {
		DEBUG_WARN("Ignoring event - stopped\n");
		spin_unlock_bh(&ecm_front_end_ipv6_lock);
		return NOTIFY_DONE;
	}
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	if (!ct) {
		DEBUG_WARN("Error: no ct\n");
		return NOTIFY_DONE;
	}

	/*
	 * handle destroy events
	 */
	if (events & (1 << IPCT_DESTROY)) {
		DEBUG_TRACE("%p: Event is destroy\n", ct);
		ecm_front_end_ipv6_conntrack_event_destroy(ct);
	}

	/*
	 * handle mark change events
	 */
	if (events & (1 << IPCT_MARK)) {
		DEBUG_TRACE("%p: Event is mark\n", ct);
		ecm_front_end_ipv6_conntrack_event_mark(ct);
	}

	return NOTIFY_DONE;
}
EXPORT_SYMBOL(ecm_front_end_ipv6_conntrack_event);

/*
 * ecm_front_end_ipv6_get_stop()
 */
static ssize_t ecm_front_end_ipv6_get_stop(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  char *buf)
{
	ssize_t count;
	int num;

	/*
	 * Operate under our locks
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	num = ecm_front_end_ipv6_stopped;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	count = snprintf(buf, (ssize_t)PAGE_SIZE, "%d\n", num);
	return count;
}

void ecm_front_end_ipv6_stop(int num)
{
	/*
	 * Operate under our locks and stop further processing of packets
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_stopped = num;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);
}
EXPORT_SYMBOL(ecm_front_end_ipv6_stop);

/*
 * ecm_front_end_ipv6_set_stop()
 */
static ssize_t ecm_front_end_ipv6_set_stop(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  const char *buf, size_t count)
{
	char num_buf[12];
	int num;

	/*
	 * Get the number from buf into a properly z-termed number buffer
	 */
	if (count > 11) {
		return 0;
	}
	memcpy(num_buf, buf, count);
	num_buf[count] = '\0';
	sscanf(num_buf, "%d", &num);
	DEBUG_TRACE("ecm_front_end_ipv6_stop = %d\n", num);

	ecm_front_end_ipv6_stop(num);

	return count;
}

/*
 * ecm_front_end_ipv6_get_udp_accelerated_count()
 */
static ssize_t ecm_front_end_ipv6_get_udp_accelerated_count(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  char *buf)
{
	ssize_t count;
	int num;

	/*
	 * Operate under our locks
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	num = ecm_front_end_ipv6_udp_accelerated_count;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	count = snprintf(buf, (ssize_t)PAGE_SIZE, "%d\n", num);
	return count;
}

/*
 * ecm_front_end_ipv6_get_tcp_accelerated_count()
 */
static ssize_t ecm_front_end_ipv6_get_tcp_accelerated_count(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  char *buf)
{
	ssize_t count;
	int num;

	/*
	 * Operate under our locks
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	num = ecm_front_end_ipv6_tcp_accelerated_count;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	count = snprintf(buf, (ssize_t)PAGE_SIZE, "%d\n", num);
	return count;
}

/*
 * ecm_front_end_ipv6_get_non_ported_accelerated_count()
 */
static ssize_t ecm_front_end_ipv6_get_non_ported_accelerated_count(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  char *buf)
{
	ssize_t count;
	int num;

	/*
	 * Operate under our locks
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	num = ecm_front_end_ipv6_non_ported_accelerated_count;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	count = snprintf(buf, (ssize_t)PAGE_SIZE, "%d\n", num);
	return count;
}

/*
 * ecm_front_end_ipv6_get_accelerated_count()
 */
static ssize_t ecm_front_end_ipv6_get_accelerated_count(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  char *buf)
{
	ssize_t count;
	int num;

	/*
	 * Operate under our locks
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	num = ecm_front_end_ipv6_accelerated_count;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	count = snprintf(buf, (ssize_t)PAGE_SIZE, "%d\n", num);
	return count;
}

/*
 * ecm_front_end_ipv6_get_no_action_limit_default()
 */
static ssize_t ecm_front_end_ipv6_get_no_action_limit_default(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  char *buf)
{
	ssize_t count;
	int num;

	/*
	 * Operate under our locks
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	num = ecm_front_end_ipv6_no_action_limit_default;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	count = snprintf(buf, (ssize_t)PAGE_SIZE, "%d\n", num);
	return count;
}

/*
 * ecm_front_end_ipv6_set_no_action_limit_default()
 */
static ssize_t ecm_front_end_ipv6_set_no_action_limit_default(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  const char *buf, size_t count)
{
	char num_buf[12];
	int num;

	/*
	 * Get the number from buf into a properly z-termed number buffer
	 */
	if (count > 11) {
		return 0;
	}
	memcpy(num_buf, buf, count);
	num_buf[count] = '\0';
	sscanf(num_buf, "%d", &num);
	DEBUG_TRACE("ecm_front_end_ipv6_no_action_limit_default = %d\n", num);

	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_no_action_limit_default = num;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	return count;
}

/*
 * ecm_front_end_ipv6_get_driver_fail_limit_default()
 */
static ssize_t ecm_front_end_ipv6_get_driver_fail_limit_default(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  char *buf)
{
	ssize_t count;
	int num;

	/*
	 * Operate under our locks
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	num = ecm_front_end_ipv6_driver_fail_limit_default;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	count = snprintf(buf, (ssize_t)PAGE_SIZE, "%d\n", num);
	return count;
}

/*
 * ecm_front_end_ipv6_set_driver_fail_limit_default()
 */
static ssize_t ecm_front_end_ipv6_set_driver_fail_limit_default(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  const char *buf, size_t count)
{
	char num_buf[12];
	int num;

	/*
	 * Get the number from buf into a properly z-termed number buffer
	 */
	if (count > 11) {
		return 0;
	}
	memcpy(num_buf, buf, count);
	num_buf[count] = '\0';
	sscanf(num_buf, "%d", &num);
	DEBUG_TRACE("ecm_front_end_ipv6_driver_fail_limit_default = %d\n", num);

	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_driver_fail_limit_default = num;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	return count;
}

/*
 * ecm_front_end_ipv6_get_nack_limit_default()
 */
static ssize_t ecm_front_end_ipv6_get_nack_limit_default(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  char *buf)
{
	ssize_t count;
	int num;

	/*
	 * Operate under our locks
	 */
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	num = ecm_front_end_ipv6_nack_limit_default;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	count = snprintf(buf, (ssize_t)PAGE_SIZE, "%d\n", num);
	return count;
}

/*
 * ecm_front_end_ipv6_set_nack_limit_default()
 */
static ssize_t ecm_front_end_ipv6_set_nack_limit_default(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  const char *buf, size_t count)
{
	char num_buf[12];
	int num;

	/*
	 * Get the number from buf into a properly z-termed number buffer
	 */
	if (count > 11) {
		return 0;
	}
	memcpy(num_buf, buf, count);
	num_buf[count] = '\0';
	sscanf(num_buf, "%d", &num);
	DEBUG_TRACE("ecm_front_end_ipv6_nack_limit_default = %d\n", num);

	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_nack_limit_default = num;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	return count;
}

/*
 * SysFS attributes
 */
static SYSDEV_ATTR(stop, 0644, ecm_front_end_ipv6_get_stop, ecm_front_end_ipv6_set_stop);
static SYSDEV_ATTR(no_action_limit_default, 0644, ecm_front_end_ipv6_get_no_action_limit_default, ecm_front_end_ipv6_set_no_action_limit_default);
static SYSDEV_ATTR(driver_fail_limit_default, 0644, ecm_front_end_ipv6_get_driver_fail_limit_default, ecm_front_end_ipv6_set_driver_fail_limit_default);
static SYSDEV_ATTR(nack_limit_default, 0644, ecm_front_end_ipv6_get_nack_limit_default, ecm_front_end_ipv6_set_nack_limit_default);
static SYSDEV_ATTR(udp_accelerated_count, 0444, ecm_front_end_ipv6_get_udp_accelerated_count, NULL);
static SYSDEV_ATTR(tcp_accelerated_count, 0444, ecm_front_end_ipv6_get_tcp_accelerated_count, NULL);
static SYSDEV_ATTR(non_ported_accelerated_count, 0444, ecm_front_end_ipv6_get_non_ported_accelerated_count, NULL);
static SYSDEV_ATTR(accelerated_count, 0444, ecm_front_end_ipv6_get_accelerated_count, NULL);

/*
 * SysFS class of the front end
 * SysFS control points can be found at /sys/devices/system/ecm_front_end_ipv6/ecm_front_end_ipv6X/
 */
static struct sysdev_class ecm_front_end_ipv6_sysclass = {
	.name = "ecm_front_end_ipv6",
};

/*
 * ecm_front_end_ipv6_init()
 */
int ecm_front_end_ipv6_init(void)
{
	int result;
	DEBUG_INFO("ECM Front end IPv6 init\n");

	/*
	 * Initialise our global lock
	 */
	spin_lock_init(&ecm_front_end_ipv6_lock);

	/*
	 * Register the sysfs class
	 */
	result = sysdev_class_register(&ecm_front_end_ipv6_sysclass);
	if (result) {
		DEBUG_ERROR("Failed to register SysFS class %d\n", result);
		return result;
	}

	/*
	 * Register SYSFS device control
	 */
	memset(&ecm_front_end_ipv6_sys_dev, 0, sizeof(ecm_front_end_ipv6_sys_dev));
	ecm_front_end_ipv6_sys_dev.id = 0;
	ecm_front_end_ipv6_sys_dev.cls = &ecm_front_end_ipv6_sysclass;
	result = sysdev_register(&ecm_front_end_ipv6_sys_dev);
	if (result) {
		DEBUG_ERROR("Failed to register SysFS device %d\n", result);
		goto task_cleanup_1;
	}

	/*
	 * Create files, one for each parameter supported by this module
	 */
	result = sysdev_create_file(&ecm_front_end_ipv6_sys_dev, &attr_stop);
	if (result) {
		DEBUG_ERROR("Failed to register stop file %d\n", result);
		goto task_cleanup_2;
	}
	result = sysdev_create_file(&ecm_front_end_ipv6_sys_dev, &attr_no_action_limit_default);
	if (result) {
		DEBUG_ERROR("Failed to register no_action_limit_default file %d\n", result);
		goto task_cleanup_2;
	}
	result = sysdev_create_file(&ecm_front_end_ipv6_sys_dev, &attr_driver_fail_limit_default);
	if (result) {
		DEBUG_ERROR("Failed to register driver_fail_limit_default file %d\n", result);
		goto task_cleanup_2;
	}
	result = sysdev_create_file(&ecm_front_end_ipv6_sys_dev, &attr_nack_limit_default);
	if (result) {
		DEBUG_ERROR("Failed to register nack_limit_default file %d\n", result);
		goto task_cleanup_2;
	}
	result = sysdev_create_file(&ecm_front_end_ipv6_sys_dev, &attr_udp_accelerated_count);
	if (result) {
		DEBUG_ERROR("Failed to register udp_accelerated_count file %d\n", result);
		goto task_cleanup_2;
	}
	result = sysdev_create_file(&ecm_front_end_ipv6_sys_dev, &attr_tcp_accelerated_count);
	if (result) {
		DEBUG_ERROR("Failed to register tcp_accelerated_count file %d\n", result);
		goto task_cleanup_2;
	}
	result = sysdev_create_file(&ecm_front_end_ipv6_sys_dev, &attr_non_ported_accelerated_count);
	if (result) {
		DEBUG_ERROR("Failed to register non_ported_accelerated_count file %d\n", result);
		goto task_cleanup_2;
	}
	result = sysdev_create_file(&ecm_front_end_ipv6_sys_dev, &attr_accelerated_count);
	if (result) {
		DEBUG_ERROR("Failed to register accelerated_count file %d\n", result);
		goto task_cleanup_2;
	}

	/*
	 * Register netfilter hooks
	 */
        result = nf_register_hooks(ecm_front_end_ipv6_netfilter_hooks, ARRAY_SIZE(ecm_front_end_ipv6_netfilter_hooks));
        if (result < 0) {
		DEBUG_ERROR("Can't register netfilter hooks.\n");
		goto task_cleanup_2;
        }

	/*
	 * Register this module with the Linux NSS Network driver
	 */
	ecm_front_end_ipv6_nss_ipv6_context = nss_ipv6_notify_register(ecm_front_end_ipv6_net_dev_callback, NULL);
	ecm_front_end_ipv6_nss_ipv6_mgr = nss_ipv6_get_mgr();

	return 0;

task_cleanup_2:
	sysdev_unregister(&ecm_front_end_ipv6_sys_dev);
task_cleanup_1:
	sysdev_class_unregister(&ecm_front_end_ipv6_sysclass);

	return result;
}
EXPORT_SYMBOL(ecm_front_end_ipv6_init);

/*
 * ecm_front_end_ipv6_exit()
 */
void ecm_front_end_ipv6_exit(void)
{
	DEBUG_INFO("ECM Front end IPv6 Module exit\n");
	spin_lock_bh(&ecm_front_end_ipv6_lock);
	ecm_front_end_ipv6_terminate_pending = true;
	spin_unlock_bh(&ecm_front_end_ipv6_lock);

	/*
	 * Stop the network stack hooks
	 */
	nf_unregister_hooks(ecm_front_end_ipv6_netfilter_hooks,
			    ARRAY_SIZE(ecm_front_end_ipv6_netfilter_hooks));

	/*
	 * Unregister from the Linux NSS Network driver
	 */
	nss_ipv6_notify_unregister();
	nss_unregister_ipv6_mgr();

	sysdev_unregister(&ecm_front_end_ipv6_sys_dev);
	sysdev_class_unregister(&ecm_front_end_ipv6_sysclass);
}
EXPORT_SYMBOL(ecm_front_end_ipv6_exit);
