/*
 * Copyright (C) 2009 Juan-Carlos Maureira, INRIA
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __INET_MACRELAYUNITSTPNP_H
#define __INET_MACRELAYUNITSTPNP_H

#include "MACRelayUnitNP.h"

#include "EtherFrame_m.h"
#include "Ethernet.h"
#include "MACAddress.h"
#include "STPTimers_m.h"
#include "BPDU_m.h"

class EtherFrame;

/**
 * An implementation of the MAC Relay Unit that assumes a shared memory and
 * N CPUs in the switch. The CPUs process frames from a single shared queue.
 */
class INET_API MACRelayUnitSTPNP : public MACRelayUnitNP
{
  private:

	struct PortStatus {

		int port_index;
		PortState state;
		PortRole role;

		STPForwardTimer* forward_timer;
		STPHoldTimer* hold_timer;
		STPPortEdgeTimer* edge_timer;

		cQueue BPDUQueue;

		PriorityVector observed_pr;
		PriorityVector proposed_pr;

		bool sync;
		bool synced;
		bool proposing;
		bool proposed;
		bool agreed;
		bool agree;

		long packet_forwarded;

		PortStatus(int port) {
			state = BLOCKING;
			role = NONDESIGNATED_PORT;
			forward_timer = NULL;
			hold_timer = NULL;
			edge_timer = NULL;
			port_index = port;
			packet_forwarded = 0;

			sync = proposing = synced = proposed = agreed = agree = false;

			std::stringstream tmp;
			tmp << "Queue BPDU port " << port;
			BPDUQueue.setName(tmp.str().c_str());

		}

		PortStatus() {
			state = BLOCKING;
			role = NONDESIGNATED_PORT;
			forward_timer = NULL;
			hold_timer = NULL;
			edge_timer = NULL;
			port_index = -1;
		}

		PortStatus(PortState s, PortRole m) {
			state = s;
			role = m;
			forward_timer = NULL;
			hold_timer = NULL;
			edge_timer = NULL;
			port_index = -1;
		}

		cMessage* getForwardTimer() {
			if (this->forward_timer==NULL) {
				this->forward_timer = new STPForwardTimer("Forward Timer");
				this->forward_timer->setKind(this->port_index);
			}
			return this->forward_timer;
		}

		void clearForwardTimer() {
			if (this->forward_timer!=NULL) {
				delete(this->forward_timer);
				this->forward_timer=NULL;
			}
		}

		STPHoldTimer* getHoldTimer() {
			if (this->hold_timer==NULL) {
				this->hold_timer= new STPHoldTimer("Hold Timer");
				this->hold_timer->setPort(this->port_index);
			}
			return this->hold_timer;
		}

		STPPortEdgeTimer* getPortEdgeTimer() {
			if (this->edge_timer==NULL) {
				this->edge_timer= new STPPortEdgeTimer("Edge Timer");
				this->edge_timer->setPort(this->port_index);
			}
			return this->edge_timer;
		}

		void clearPortEdgeTimer() {
			if (this->edge_timer!=NULL) {
				delete(this->edge_timer);
				this->edge_timer=NULL;
			}
		}

		bool isHoldTimerActive() {
			if (this->hold_timer!=NULL) {
				if (this->hold_timer->isScheduled()) {
					return true;
				}
			}
			return false;
		}

		bool isPortEdgeTimerActive() {
			if (this->edge_timer!=NULL) {
				if (this->edge_timer->isScheduled()) {
					return true;
				}
			}
			return false;
		}

		friend std::ostream& operator << (std::ostream& os, const PortStatus& p ) {

			switch (p.state) {
			case BLOCKING:
				os << "BLOCKING";
				break;
			case LISTENING:
				os << "LISTENING";
				break;;
			case LEARNING:
				os << "LEARNING";
				break;;
			case FORWARDING:
				os << "FORWARDING";
				break;;
			}

			os << "-";

			switch (p.role) {
			case ROOT_PORT:
				os << "ROOT_PORT";
				break;
			case DESIGNATED_PORT:
				os << "DESIGNATED_PORT";
				break;;
			case NONDESIGNATED_PORT:
				os << "NONDESIGNATED_PORT";
				break;;
			case ALTERNATE_PORT:
				os << "ALTERNATE_PORT";
				break;;
			case BACKUP_PORT:
				os << "BACKUP_PORT";
				break;;
			case EDGE_PORT:
				os << "EDGE_PORT";
				break;;
			}

			return os;
		}

	};

	// switch port status
	typedef std::map<int,PortStatus> PortStatusList;
	PortStatusList port_status;

	// timer timeouts
	int hello_time;
	int max_age_time;
	int forward_delay;
	int message_age;
	int hold_time;
	int migrate_delay;
	int edge_delay;
	int packet_fwd_limit;

	// timer messages
	cMessage* hello_timer;
	cMessage* bpdu_timeout_timer;

	// STP parameters
	BridgeID bridge_id;         // bridge id
	simtime_t power_on_time;    // time where the bridge starts its operation
	simtime_t topology_change_timeout; // time where the STP flags the BPDU as topology change
	simtime_t original_mac_aging_time; // original mac aging delay
	int bpdu_timeout;                  // time without receiving a bpdu from the root bridge
	                                   // and considers the root port lost

	bool active;                       // Protocol is active
	bool allSynced;                    // RSTP synced ports to allow fast transitions

	// priority vector identifying the root bridge (according 802.1w)
	PriorityVector priority_vector;

	// helper methods
	void setRootPort(int port);
	int getRootPort();
	void recordPriorityVector(BPDU* bpdu, int port_idx);
	void recordRootTimerDelays(BPDU* bpdu);
	void setAllPortsStatus(PortStatus status);
	void setPortStatus(int port_idx, PortStatus status);
	void scheduleHoldTimer(int port);
	void scheduleHelloTimer();
	void restartBPDUTTLTimer();
	void cancelBPDUTTLTimer();
	BPDU* getNewBPDU(BPDUType type); // BPDU factory

	void flushMACAddressesOnPort(int port_idx);
	void moveMACAddresses(int from_port,int to_port);

  public:

	static const MACAddress STPMCAST_ADDRESS;

    MACRelayUnitSTPNP();
    virtual ~MACRelayUnitSTPNP();

  protected:

	virtual void initialize();
	virtual void handleMessage(cMessage* msg);
	virtual void handleTimer(cMessage* t);
	void broadcastFrame(EtherFrame *frame, int inputport);

	// Handling incoming Ethernet frame
	virtual void handleIncomingFrame(EtherFrame *msg);

	// process incoming BPDU's
	virtual void handleBPDU(BPDU* bpdu);
	virtual void handleConfigurationBPDU(CBPDU* bpdu);
	virtual void handleTopologyChangeNotificationBPDU(TCNBPDU* bpdu);

	// send BPDU's
	virtual void sendConfigurationBPDU(int port_idx=-1);
	virtual void sendTopologyChangeNotificationBPDU(int port_idx);
	virtual void sendTopologyChangeAckBPDU(int port_idx);

	virtual void sendBPDU(BPDU* bpdu,int port=-1);

};

#endif

