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

#include "MACRelayUnitSTPNP.h"
#include "EtherFrame_m.h"
#include "Ethernet.h"
#include "MACAddress.h"

// Next Day TODO:

// 3. evaluate all scenarios with random powerOn times (to test when the topology converge) and then
//    with link failures to evaluate the fast recovery when backups/alternate links are presents
// 4. send the TC when topology changes to shorten the MAC aging timers

Define_Module( MACRelayUnitSTPNP );

const MACAddress MACRelayUnitSTPNP::STPMCAST_ADDRESS("01:80:C2:00:00:00");

MACRelayUnitSTPNP::MACRelayUnitSTPNP() {
	MACRelayUnitNP::MACRelayUnitNP();
	this->active = false;
	this->bpdu_timeout_timer = NULL;
	this->hello_timer = NULL;
	this->topology_change_timeout = 0;
	this->message_age = 0;
}

MACRelayUnitSTPNP::~MACRelayUnitSTPNP() {

}

void MACRelayUnitSTPNP::setAllPortsStatus(PortStatus status) {
	for(int i=0;i<this->gateSize("lowerLayerOut");i++) {
		this->setPortStatus(i,status);
	}
}

void MACRelayUnitSTPNP::setPortStatus(int port_idx, PortStatus status) {
	if (this->port_status.find(port_idx) != this->port_status.end()) {

		if (this->port_status[port_idx].role == status.role && this->port_status[port_idx].state == status.state) {
			// port is in the same status, no need to change port status
			return;
		}

		this->port_status[port_idx].role = status.role;
		this->port_status[port_idx].state = status.state;

		this->port_status[port_idx].BPDUQueue.clear(); // clean the BPDU queue since the port status have change

		if (status.state == LEARNING && status.role == ROOT_PORT) {
			this->port_status[port_idx].sync      = true;   // root port was proposed
			this->port_status[port_idx].proposing = false;  // root port is not proposing
			this->port_status[port_idx].proposed  = true;   // root port was proposed
			this->port_status[port_idx].agree     = false;  // wait for the allSynced to agree
			this->port_status[port_idx].agreed    = false;  // reset this flag
		}

		if ((status.state == LISTENING || status.state == LEARNING) && status.role == DESIGNATED_PORT) {
			// we are in position to propose
			this->port_status[port_idx].sync = true;      // we are in the sync process
			this->port_status[port_idx].proposing = true; // we start proposing until we block/edge the port
			this->port_status[port_idx].agreed = false;   // wait to be agreed with the proposal
			this->port_status[port_idx].agree = false;    // reset this flag
			this->port_status[port_idx].synced = false;   // wait until we agreed
		}

		if (status.state == FORWARDING && status.role == DESIGNATED_PORT) {
			// we are agreed to change to forward on this port
			this->port_status[port_idx].sync = false;      // sync finished
			this->port_status[port_idx].proposing = false; // reset this flag
			this->port_status[port_idx].proposed = false; // reset this flag
			this->port_status[port_idx].agreed = true;   // port is agreed
			this->port_status[port_idx].synced = true;   // port synced
		}

		if (status.role == EDGE_PORT || status.role == BACKUP_PORT) {
			// this port is synced
			this->port_status[port_idx].sync = true;      // sync process finished
			this->port_status[port_idx].synced = true;     // port synced
			this->port_status[port_idx].proposing = false; // reset this flag
		}

		if (status.role == ALTERNATE_PORT) {
			// this port is synced
			this->port_status[port_idx].sync = true;      // sync process finished
			this->port_status[port_idx].synced = true;    // port synced
			this->port_status[port_idx].agree = true;     // agree to set this port in alternate mode
			this->port_status[port_idx].proposing = false; // reset this flag
		}

		EV << "Port " << port_idx << " change status : " << status << endl;
		if (status.state == BLOCKING) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("ls",0,"red");
			if (this->port_status[port_idx].getForwardTimer()->isScheduled()) {
				EV << "  Canceling fwd timer" << endl;
				cancelEvent(this->port_status[port_idx].getForwardTimer());
			}
		} else if (status.state == LISTENING) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("ls",0,"yellow");
			// schedule the port edge timer
			if (this->port_status[port_idx].getPortEdgeTimer()->isScheduled()) {
				EV << "  Canceling port edge timer" << endl;
				cancelEvent(this->port_status[port_idx].getPortEdgeTimer());
			}
			this->scheduleAt(simTime()+this->edge_delay,this->port_status[port_idx].getPortEdgeTimer());
			// schedule the forward timer
			if (this->port_status[port_idx].getForwardTimer()->isScheduled()) {
				EV << "  Canceling fwd timer" << endl;
				cancelEvent(this->port_status[port_idx].getForwardTimer());
			}
			EV << "  restarting the forward timer" << endl;
			this->scheduleAt(simTime()+this->forward_delay,this->port_status[port_idx].getForwardTimer());

		} else if (status.state == LEARNING) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("ls",0,"blue");

			// check the edge timer and if it is active, canceling it since
			if (this->port_status[port_idx].isPortEdgeTimerActive()) {
				EV << "  Canceling port edge timer" << endl;
				cancelEvent(this->port_status[port_idx].getPortEdgeTimer());
				this->port_status[port_idx].clearPortEdgeTimer();
			}
			if (this->port_status[port_idx].getForwardTimer()->isScheduled()) {
				EV << "  Canceling fwd timer" << endl;
				cancelEvent(this->port_status[port_idx].getForwardTimer());
			}
			EV << "  restarting the forward timer" << endl;
			this->scheduleAt(simTime()+this->forward_delay,this->port_status[port_idx].getForwardTimer());

			// flush entries for this port to start the learning process
			this->flushMACAddressesOnPort(port_idx);

		} else if (status.state == FORWARDING) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("ls",0,"green");
			if (this->port_status[port_idx].getForwardTimer()->isScheduled()) {
				EV << "  Canceling fwd timer" << endl;
				cancelEvent(this->port_status[port_idx].getForwardTimer());
			}

			if (this->port_status[port_idx].isPortEdgeTimerActive()) {
				EV << "  Canceling port edge timer" << endl;
				cancelEvent(this->port_status[port_idx].getPortEdgeTimer());
				this->port_status[port_idx].clearPortEdgeTimer();
			}
			this->port_status[port_idx].clearForwardTimer();
		}

		if (status.role == ROOT_PORT) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("t",0,"R");
		} else if (status.role == DESIGNATED_PORT) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("t",0,"D");
		} else if (status.role == ALTERNATE_PORT) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("t",0,"A");
		} else if (status.role == BACKUP_PORT) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("t",0,"B");
		} else if (status.role == NONDESIGNATED_PORT) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("t",0,"ND");
		} else if (status.role == EDGE_PORT) {
			this->gate("lowerLayerOut",port_idx)->getDisplayString().setTagArg("t",0,"E");
		}
	} else {
		error("trying to change the status to a port that is not registered into the portStatusList into this switch");
	}

	// check for allSynced flag
	this->allSynced = true;
	for(int i=0;i<this->gateSize("lowerLayerOut");i++) {
		if (i!=this->getRootPort()) {
			if (!this->port_status[i].synced) {
				// there are still ports to be synced.
				this->allSynced = false;
			}
		}
	}

	// check root port agreement for non-root bridges only
	if (this->getRootPort()>-1) {
		// check if all ports are synced when the root port is not yet agreed.
		if (this->allSynced) {
			if (!this->port_status[this->getRootPort()].agree) {
				// agree the root port and set the root port in forward mode immediately
				EV << "all ports synced, so, we agree with the root port selection" << endl;
				this->port_status[this->getRootPort()].agree = true;
				this->setPortStatus(this->getRootPort(),PortStatus(FORWARDING,ROOT_PORT));

				this->sendConfigurationBPDU(this->getRootPort());
			}
		}
	}

}

void MACRelayUnitSTPNP::setRootPort(int port) {

	// start the sync process on all the ports
	this->allSynced = false;
	EV << "Old Root Election. PR " << this->priority_vector << endl;
	this->priority_vector = this->port_status[port].observed_pr;
	EV << "New Root Election: " << this->priority_vector << endl;

	// Setting all the port in designated status
	for(int i=0;i<this->gateSize("lowerLayerOut");i++) {
		if (i!=port) {
			this->setPortStatus(i,PortStatus(LISTENING,DESIGNATED_PORT));
		}
	}
	// assing the root port role to the given port
	this->setPortStatus(port,PortStatus(LEARNING,ROOT_PORT));

	// start the BPDU ttl timer to know when we have lost the root port
	this->restartBPDUTTLTimer();
	// schedule the hello timer according the values received from the root bridge (RSTP)
	this->scheduleHelloTimer();

	// start proposing our information to all the ports
	this->sendConfigurationBPDU();
}

void MACRelayUnitSTPNP::recordRootTimerDelays(BPDU* bpdu) {
	// record the root bridge timers information and message age
	this->max_age_time = bpdu->getMaxAge();
	this->forward_delay = bpdu->getForwardDelay();
	this->hello_time = bpdu->getHelloTime();
	this->message_age = bpdu->getMessageAge();
}

void MACRelayUnitSTPNP::recordPriorityVector(BPDU* bpdu, int port_idx) {
	this->port_status[port_idx].observed_pr = PriorityVector(bpdu->getRootBID(),bpdu->getRootPathCost(),bpdu->getSenderBID(),bpdu->getPortId());
}


int MACRelayUnitSTPNP::getRootPort() {
	for(int i=0;i<this->gateSize("lowerLayerOut");i++) {
		if (this->port_status[i].role == ROOT_PORT) {
			return i;
		}
	}
	return -1;
}

void MACRelayUnitSTPNP::scheduleHoldTimer(int port) {
	STPHoldTimer* hold_timer = this->port_status[port].getHoldTimer();

	if (hold_timer->isScheduled()) {
		EV << "canceling and rescheduling hold timer on port " << port << " " << endl;
		cancelEvent(hold_timer);
	} else {
		EV << "scheduling hold timer on port " << port << " " << endl;
	}
	this->port_status[port].packet_forwarded = 0;
	scheduleAt(simTime()+this->hold_time,hold_timer);
}

void MACRelayUnitSTPNP::scheduleHelloTimer() {

    if (this->hello_timer==NULL) {
    	this->hello_timer = new STPHelloTimer("STP Hello Timer");
    }

    if (this->hello_timer->isScheduled()) {
    	cancelEvent(this->hello_timer);
    }

    if (this->hello_time>0) {
    	EV << "Scheduling Hello timer to " << simTime()+this->hello_time << endl;
    	scheduleAt(simTime()+this->hello_time,this->hello_timer);
    } else {
    	error("Hello timer could not be scheduled due to hello time is invalid");
    }
}


void MACRelayUnitSTPNP::restartBPDUTTLTimer() {

	EV << "Rescheduling BPDU Timeout Timer" << endl;

	if (this->bpdu_timeout_timer==NULL) {
		this->bpdu_timeout_timer = new STPBPDUTTLTimer("BPDU Timeout");
	}

	if (this->bpdu_timeout_timer->isScheduled()) {
		cancelEvent(this->bpdu_timeout_timer);
		scheduleAt(simTime()+this->bpdu_timeout,this->bpdu_timeout_timer);
	} else {
		scheduleAt(simTime()+this->bpdu_timeout,this->bpdu_timeout_timer);
	}
}

void MACRelayUnitSTPNP::cancelBPDUTTLTimer() {

	EV << "Canceling BPDU Timeout Timer" << endl;

	if (this->bpdu_timeout_timer!=NULL) {
		if (this->bpdu_timeout_timer->isScheduled()) {
			cancelEvent(this->bpdu_timeout_timer);
		}
		delete(this->bpdu_timeout_timer);
		this->bpdu_timeout_timer = NULL;
	}
}

void MACRelayUnitSTPNP::flushMACAddressesOnPort(int port_idx) {
	EV << "Flushing MAC Address on port " << port_idx << endl;
    for (AddressTable::iterator iter = addresstable.begin(); iter != addresstable.end();)
    {
        AddressTable::iterator cur = iter++; // iter will get invalidated after erase()
        AddressEntry& entry = cur->second;
        if (entry.portno == port_idx ) {
            EV << "Removing entry from Address Table: " <<
                  cur->first << " --> port" << cur->second.portno << "\n";
            addresstable.erase(cur);
        }
    }
}

void MACRelayUnitSTPNP::moveMACAddresses(int from_port,int to_port) {
	EV << "Moving MAC Address from port " << from_port <<  " to port " << to_port << endl;
	for (AddressTable::iterator iter = addresstable.begin(); iter != addresstable.end();)
	{
		AddressTable::iterator cur = iter++;
		MACAddress mac = cur->first;
		AddressEntry& entry = cur->second;
		if (entry.portno == from_port ) {
			this->updateTableWithAddress(mac,to_port);
		}
	}
}


void MACRelayUnitSTPNP::initialize() {

	MACRelayUnitNP::initialize();

	EV << "STP Initialization" << endl;

    this->bridge_id.priority = par("priority");
	this->hello_time         = par("helloTime");
	this->max_age_time       = par("maxAge");
	this->forward_delay      = par("forwardDelay");
	this->power_on_time      = par("powerOn");
	this->hold_time          = par("holdTime");
	this->migrate_delay      = par("migrateDelay");
	this->edge_delay         = par("portEdgeDelay");
	this->packet_fwd_limit   = par("packetFwdLimit");
	this->bpdu_timeout       = par("bpduTimeout");

	// capture the original addresses aging time to restore it when
	// the TCN will change this delay to get a faster renew of the address table.
	this->original_mac_aging_time = par("agingTime");

    const char* address_string = par("bridgeAddress");
    if (!strcmp(address_string,"auto")) {
        // assign automatic address
    	this->bridge_id.address = MACAddress::generateAutoAddress();
        // change module parameter from "auto" to concrete address
        par("bridgeAddress").setStringValue(this->bridge_id.address.str().c_str());
    } else {
    	this->bridge_id.address.setAddress(par("bridgeAddress"));
    }

    // initially, we state that we are the root bridge

    this->priority_vector = PriorityVector(this->bridge_id,0,this->bridge_id,0);

    EV << "Bridge ID :" << this->bridge_id << endl;
    EV << "Root Priority Vector :" << this->priority_vector << endl;

    scheduleAt(this->power_on_time, new STPStartProtocol("PoweringUp the Bridge"));

    // switch port registration
    this->port_status.clear();
    for(int i=0;i<this->gateSize("lowerLayerOut");i++) {
    	this->port_status.insert(std::make_pair(i,PortStatus(i)));
    }

    WATCH(bridge_id);
    WATCH(priority_vector);
    WATCH(message_age);

}

void MACRelayUnitSTPNP::handleMessage(cMessage* msg) {
	if (msg->isSelfMessage()) {
		if (dynamic_cast<STPTimer*>(msg)) {
			this->handleTimer(msg);
		} else {
			this->processFrame(msg);
		}
	} else {
		if (this->active) {
			if (dynamic_cast<EtherFrame*>(msg)) {
				cPacket* frame = ((EtherFrame*)msg)->getEncapsulatedMsg();
				if (dynamic_cast<BPDU*>(frame)) {
					EV << "Incoming BPDU via Port " << msg->getArrivalGate()->getIndex() << ". Processing it" << endl;
					cPacket* frame = ((EtherFrame*)msg)->decapsulate();
					BPDU* bpdu = dynamic_cast<BPDU*>(frame);
					bpdu->setArrival(this,msg->getArrivalGateId());
					this->handleBPDU(bpdu);

					delete (msg);
				} else {
					// ether_frame incoming, check the arrival port ain't blocked
					if (this->port_status[msg->getArrivalGate()->getIndex()].state != BLOCKING) {

						EtherFrame* frame = dynamic_cast<EtherFrame*>(msg);
						int outputport = getPortForAddress(frame->getDest());

						if (outputport>=0 && this->port_status[outputport].state!=FORWARDING) {

							if (this->port_status[outputport].state==LEARNING) {
								EV << "Port in LEARNING state. updating address table" << endl;
								this->updateTableWithAddress(frame->getDest(),outputport);
							}

							EV << "Frame arrived on port " << msg->getArrivalGate()->getIndex() << " Addressed to a port not in forward mode. discarding it" << endl;
							delete(frame);
							return;
						}

						if (outputport>-1) {
							this->port_status[outputport].packet_forwarded++;
							if (this->port_status[outputport].packet_forwarded > this->packet_fwd_limit) {
								// possible loop.. forcing a bpdu transmission
								EV << "packet forward limit reached on port " << outputport << " forcing a bpdu transmission " << endl;
								this->sendConfigurationBPDU(outputport);
								this->port_status[outputport].packet_forwarded = 0;
							}
							EV << "Port " << outputport << " forwarded frames " << this->port_status[outputport].packet_forwarded << endl;
						}
						this->handleIncomingFrame(frame);
					} else {
						// discarding frame since the arrival port is blocked
						EV << "Port " << msg->getArrivalGate()->getIndex() << " incoming frame in blocked port. discarding" << endl;
						delete(msg);
					}
				}
			} else {
				EV << "Unknown frame time arrived" << endl;
			}
		} else {
			EV << "Bridge not active. discarding packet" << endl;
			delete(msg);
		}
	}
}

void MACRelayUnitSTPNP::handleTimer(cMessage* msg) {
	if (dynamic_cast<STPStartProtocol*>(msg)) {
		this->active = true;

		EV << "Starting RSTP Protocol." << endl;

		this->message_age = 0;
		this->priority_vector = PriorityVector(this->bridge_id,0,this->bridge_id,0);

		EV << "I'm the ROOT Bridge: Priority Vector: " << this->priority_vector << endl;

		this->setAllPortsStatus(PortStatus(LISTENING,DESIGNATED_PORT));
		this->cancelBPDUTTLTimer();
		this->sendConfigurationBPDU();
		this->scheduleHelloTimer();

		delete(msg);
	} else if (dynamic_cast<STPHelloTimer*>(msg)) {
		EV << "Hello Timer arrived" << endl;
		this->sendConfigurationBPDU();
		// schedule the timer again to the hello time
		this->scheduleHelloTimer();

	} else if (dynamic_cast<STPForwardTimer*>(msg)) {
		int port = msg->getKind();
		EV << "Forward timer arrived for port " << port << endl;

		if (this->port_status[port].state == LISTENING) {
			EV << "Port transition from LISTENING to LEARNING" << endl;
			this->setPortStatus(port, PortStatus(LEARNING,this->port_status[port].role));
		}
		if (this->port_status[port].state == LEARNING) {
			EV << "Port transition from LEARNING to FORWARDING" << endl;
			this->setPortStatus(port, PortStatus(FORWARDING,this->port_status[port].role));
		}

		// check topology change
		if (this->priority_vector.root_id != this->bridge_id) {
			// if i'm not the root.
			if (port!=this->getRootPort()) {
				// send the TCN  via the root port informing "port" have change state
				this->sendTopologyChangeNotificationBPDU(port);
			} else {
				// root port have change state, we dont need to inform it.
			}
		} else {
			// i'm the root, set the topology change timeout
			this->topology_change_timeout = simTime() + this->max_age_time + this->forward_delay;
			EV << "setting the topology change timeout to " << this->topology_change_timeout << endl;
		}

	} else if (dynamic_cast<STPHoldTimer*>(msg)) {
		STPHoldTimer* hold_timer = dynamic_cast<STPHoldTimer*>(msg);
		int port = hold_timer->getPort();
		EV << "Hold timer arrived for port " << port << endl;
		if (!this->port_status[port].BPDUQueue.isEmpty()) {
			BPDU* bpdu = (BPDU*)this->port_status[port].BPDUQueue.pop();
			this->sendBPDU(bpdu,port);
		}
	} else if (dynamic_cast<STPBPDUTTLTimer*>(msg)) {
		EV << "BPDU TTL timeout arrived, Root Port lost" << endl;

		// FastRecovery procedure when there is a backup root path
		int root_candidate = -1;
		for(int i=0;i<this->gateSize("lowerLayerOut");i++) {
			if (this->port_status[i].role == BACKUP_PORT && this->port_status[i].observed_pr.bridge_id == this->priority_vector.bridge_id) {
				root_candidate = i;
				break;
			}
		}

		if (root_candidate>-1) {
			// there is a candidate to replace the lost root port
			// setting the old root port in designated mode
			int lost_root_port = this->getRootPort();
			EV << "replace lost root " << lost_root_port << " port by port " << root_candidate << " port status: " <<  this->port_status[root_candidate]<< endl;
			this->setPortStatus(root_candidate,PortStatus(LEARNING,ROOT_PORT));
			this->setPortStatus(lost_root_port,PortStatus(LISTENING,DESIGNATED_PORT));
			this->priority_vector = this->port_status[root_candidate].observed_pr;

			EV << "New Root Election: " << this->priority_vector << endl;
			// moving mac address from old root port to the new one
			this->moveMACAddresses(lost_root_port,root_candidate);

			// start the BPDU maxAge timer to know when we have lost the root port
			this->restartBPDUTTLTimer();
			// schedule the hello timer according the values received from the root bridge (RSTP)
			this->scheduleHelloTimer();
			// start updating our information to all the ports
			this->sendConfigurationBPDU();

		} else {
			// i'm the root switch.
			this->handleTimer(new STPStartProtocol("Restart the RSTP Protocol"));
		}
	} else if (dynamic_cast<STPPortEdgeTimer*>(msg)) {
		STPPortEdgeTimer* edge_timer = dynamic_cast<STPPortEdgeTimer*>(msg);
		int port = edge_timer->getPort();
		EV << "Port Edge timer arrived for port. passing it to forward state immediately " << port << endl;
		this->setPortStatus(port,PortStatus(FORWARDING,EDGE_PORT));
		this->port_status[port].clearPortEdgeTimer();
	}

}

// process incoming BPDU's
void MACRelayUnitSTPNP::handleBPDU(BPDU* bpdu) {
	int arrival_port = bpdu->getArrivalGate()->getIndex();
	EV << bpdu->getName() << " arrived on port " << arrival_port << endl;


	// check the message age limit
	if (bpdu->getMessageAge() > this->max_age_time) {
		EV << "Incoming BPDU age " << bpdu->getMessageAge() << " exceed the max age time " << this->max_age_time <<  ", blocking port" << endl;
		this->setPortStatus(arrival_port,PortStatus(BLOCKING,ALTERNATE_PORT));
		delete(bpdu);
		return;
	}


	// cancel and clear the Port Edge if it exists
	if (this->port_status[arrival_port].isPortEdgeTimerActive()) {
		EV << "  Canceling port edge timer" << endl;
		cancelEvent(this->port_status[arrival_port].getPortEdgeTimer());
		this->port_status[arrival_port].clearPortEdgeTimer();
	}

	// if port is in listening mode, set it in learning mode, since there is a bridge connected
	if (this->port_status[arrival_port].state == LISTENING) {
		EV << "changing port " << arrival_port << " to learning state since there is a bridge connected" << endl;
		this->setPortStatus(arrival_port,PortStatus(LEARNING,this->port_status[arrival_port].role));
	}

	// if i'm not the root, check the bpdu ttl timer
	if (this->priority_vector.root_id != this->bridge_id) {
		// if the BPDU comes from the root port, update the bpdu ttl timer
		if (bpdu->getArrivalGate()->getIndex() == this->getRootPort()) {
			this->restartBPDUTTLTimer();
		}
	}

	// check if topology change flag is set to reduce the aging mac timer
	if (bpdu->getTopologyChangeFlag()) {
		this->agingTime = this->max_age_time + this->forward_delay;
		this->par("agingTime") = this->agingTime.dbl();
		EV << "BPDU is topology change flagged, reducing the mac aging time = " << this->agingTime << endl;
	} else {
		this->agingTime = this->original_mac_aging_time;
		this->par("agingTime") = this->agingTime.dbl();
		EV << "BPDU is NOT topology change flagged, mac aging time = " << this->agingTime << endl;
	}

	if (bpdu->getType() == CONF_BPDU) {
		this->handleConfigurationBPDU(check_and_cast<CBPDU*>(bpdu));
	} else if (bpdu->getType() == TCN_BPDU) {
		this->handleTopologyChangeNotificationBPDU(check_and_cast<TCNBPDU*>(bpdu));
	} else {
		EV << "Unknown BPDU, ignoring it" << endl;
	}
}
void MACRelayUnitSTPNP::handleConfigurationBPDU(CBPDU* bpdu) {

	int arrival_port = bpdu->getArrivalGate()->getIndex();
	PriorityVector arrived_pr = PriorityVector(bpdu->getRootBID(),bpdu->getRootPathCost(),bpdu->getSenderBID(),bpdu->getPortId());
	this->recordPriorityVector(bpdu,arrival_port);
	EV << "BPDU age " << bpdu->getMessageAge() << " current message age " << this->message_age << endl;

	EV << "Arrived PR: " << arrived_pr << endl;
	if (this->port_status[arrival_port].proposing) {
		EV << "Proposing PR: " << this->port_status[arrival_port].proposed_pr << endl;
	}
	if (this->port_status[arrival_port].proposed) {
		EV << "Proposed PR: " << this->port_status[arrival_port].observed_pr << endl;
	}
	EV << "ROOT PR: " << this->priority_vector << endl;

	// RSTP: check for port agreement allowing port fast transition
	if (arrived_pr == this->port_status[arrival_port].proposed_pr) {
		// arrived bpdu is exactly the same that we have proposed. bpdu should be an agreement and the port should be proposing
		if (this->port_status[arrival_port].proposing && bpdu->getAgreement()) {
			EV << "BPDU agrees the proposal to fast port transition. set the port in FORWARDING state" << endl;
			this->setPortStatus(arrival_port,PortStatus(FORWARDING,DESIGNATED_PORT));
		} else {
			EV << "BPDU comes from my self (same port!!). discarding it" << endl;
		}
		delete(bpdu);
		return;
	}

	if (arrived_pr > this->priority_vector) {
		EV << "received BPDU has superior priority vector." << endl;
		if (this->priority_vector.root_id == this->bridge_id) {
			EV << "This switch is no longer the root bridge" << endl;
		}
		// set the root port and the other port status (initially designated)
		this->recordRootTimerDelays(bpdu);
		this->setRootPort(arrival_port);

		EV << "current BPDU Age counter " << this->message_age << endl;

	} else if (arrived_pr == this->priority_vector) {
		EV << "Root Bridge informed is the same. current root:" <<  this->priority_vector << endl;
		if (arrival_port < this->getRootPort()) {
			EV << "new bpdu informs a new path to the root tie-breaking by the local port id" << endl;

			if (this->priority_vector.root_id == this->bridge_id) {
				EV << "This switch is no longer the root bridge" << endl;
			}
			// set the root port and the other port status (initially designated)
			this->recordRootTimerDelays(bpdu);
			this->setRootPort(arrival_port);

		} else if (this->getRootPort()>-1 && arrival_port > this->getRootPort()) {
			// same root bridge bpdu arrived on a lower priority port. blocking the port and leaving it as backup
			// NOTE: this port should be in Alternate mode according the RSTP protocol. but this case happened
			// when both bridges are connected by two parallel links. So it makes more sense to assigned this port
			// as backup in order to allow a faster recovery when the root port is lost.
			EV << "same root bridge BPDU arrived on a lower priority port. blocking the port and leave it in backup role" << endl;
			this->setPortStatus(arrival_port,PortStatus(BLOCKING,BACKUP_PORT));
		} else {
			if (this->getRootPort()>-1) {
				EV << "Keeping the root election and updating message age" << endl;
				this->message_age = bpdu->getMessageAge();
			} else {
				// root port blocked by a redundant path before to get blocked. reactivating the root port
				EV << "root port blocked by a redundant path before to get blocked. reactivating the root port" << endl;
				this->recordRootTimerDelays(bpdu);
				this->setRootPort(arrival_port);
			}
		}
	} else {
		EV << "received BPDU does have inferior priority vector." << endl;

		if (arrived_pr.root_id == this->priority_vector.root_id) {
			EV << "BPDU informing the same root bridge we have selected" << endl;

			if (arrived_pr.root_path_cost > this->priority_vector.root_path_cost) {
				if (arrived_pr.bridge_id != this->bridge_id) {

					if (arrived_pr.bridge_id < this->bridge_id) {

						EV << "BPDU informs a lower priority switch connected to this network segment. port status " << this->port_status[arrival_port] << endl;

					} else {
						if (bpdu->getPortRole()==ROOT_PORT) {
							EV << "Inferior BPDU arrived from a root port. RSTP special case: Accepting it" << endl;
							if (this->port_status[arrival_port].role==DESIGNATED_PORT) {
								EV << "Proposing a path to the root bridge" << endl;
								this->port_status[arrival_port].proposing = true;
								this->sendConfigurationBPDU(arrival_port);
							} else {
								EV << "lower priority BPDU arrived in a non designated port. just ignoring it" << endl;
							}
						} else {
							EV << "BPDU informs that this port is an higher cost alternate path to the root. this port is BLOCKED/ALTERNATE_PORT" << endl;
							this->setPortStatus(arrival_port,PortStatus(BLOCKING,ALTERNATE_PORT));
						}
					}
				} else {
					// BPDU comes from my self with a higher cost
					if (arrival_port > arrived_pr.port_id) {
						EV << "BPDU informs a backup designated path to the same network segment. this port is BLOCKED/BACKUP_PORT" << endl;
						this->setPortStatus(arrival_port,PortStatus(BLOCKING,BACKUP_PORT));
					} else {
						EV << "BPDU from my self arrived with better priority arrived. discard it" << endl;
					}
				}
			} else if (arrived_pr.root_path_cost < this->priority_vector.root_path_cost) {
				EV << "BPDU informs an alternate better path to the root. This is the new ROOT port" << endl;
				if (this->priority_vector.root_id == this->bridge_id) {
					EV << "This switch is no longer the root bridge" << endl;
				}
				// set the root port and the other port status (initially designated)
				this->recordRootTimerDelays(bpdu);
				this->setRootPort(arrival_port);
			} else {
				// root paths cost and arrived bpdu informs similar costs. analyzing the bridge_ids
				if (arrived_pr.bridge_id < this->priority_vector.bridge_id && arrived_pr.bridge_id != this->bridge_id) {
					EV << "BPDU informs an same cost alternate path to the root bridge via a lower priority bridge. Port is BLOCKED/ALTERNATE_PORT" << endl;
					this->setPortStatus(arrival_port,PortStatus(BLOCKING,ALTERNATE_PORT));
				} else {
					if (arrived_pr.bridge_id != this->bridge_id) {
						// BPDU comes from another bridge
						if (arrived_pr.bridge_id == this->priority_vector.bridge_id) {
							EV << "BPDU informs a root backup path to the same network segment. this port is BLOCKED/BACKUP_PORT" << endl;
							this->setPortStatus(arrival_port,PortStatus(BLOCKING,BACKUP_PORT));
						} else {
							// TODO: untie from sender port id
							EV << "unhandled case." << endl;
						}
					} else {
						// BPDU arrived from my self
						if (arrival_port != this->getRootPort()) {
							// but from a different port than root port
							if (arrival_port > this->getRootPort()) {
								EV << "BPDU informs a backup path to the same network segment. this port is BLOCKED/BACKUP_PORT" << endl;
								this->setPortStatus(arrival_port,PortStatus(BLOCKING,BACKUP_PORT));

							} else if (arrival_port < this->getRootPort()) {
								EV << "BPDU informs that this port is the best port connected to a network segment. this port is DESIGNATED" << endl;
								this->setPortStatus(arrival_port,PortStatus(this->port_status[arrival_port].state,DESIGNATED_PORT));
								EV << "Proposing a path to the root bridge" << endl;
								this->port_status[arrival_port].proposing = true;
								this->sendConfigurationBPDU(arrival_port);
							} else {
								// this case does not exists, since priority vectors are equal, case already handled
								error("BPDU equal to the root priority vector, but identified as a low priority one. this situation should never happened");
							}
						} else {
							// on the root port. discarding it
							EV << "BPDU from my self arrived on my root port. discarding it" << endl;
						}
					}
				}
			}
		} else {
			EV << "BPDU informing a lower priority bridge as root bridge." << endl;
			if (this->port_status[arrival_port].role !=ROOT_PORT) {
				EV << "This port becomes DESIGNATED." << endl << "Proposing a path to our root bridge" << endl;
				this->setPortStatus(arrival_port,PortStatus(LEARNING,DESIGNATED_PORT));
				this->port_status[arrival_port].proposing = true;
				this->port_status[arrival_port].proposed = false;
				this->sendConfigurationBPDU(arrival_port);
			} else {
				EV << "This port becomes DESIGNATED." << endl << "We lost the root port. reinitializing protocol." << endl;
				this->priority_vector = PriorityVector(this->bridge_id,0,this->bridge_id,0);
				EV << "i'm the Root Bridge" << endl;
				this->handleTimer(new STPStartProtocol("Restart the RSTP Protocol"));
			}
		}

	}

	delete(bpdu);
}
void MACRelayUnitSTPNP::handleTopologyChangeNotificationBPDU(TCNBPDU* bpdu) {
	int port = bpdu->getArrivalGate()->getIndex();

	if (this->priority_vector.root_id == this->bridge_id) {

		delete(bpdu);

		// TCN received at the root bridge, ack it and configure the topology change flag timeout
		// According the STP, we flag the BPDU with the Topology Change flag by a period of this->max_age_time + this->forward_delay

		this->topology_change_timeout = simTime()+this->max_age_time + this->forward_delay;

		EV << "setting the topology change timeout to " << this->topology_change_timeout << " and send the ACK on the same port (" << port << ")" << endl;

		this->sendTopologyChangeAckBPDU(port);

	} else {

		if (this->getRootPort() == port) {
			EV << "TCN arrived on the root port. discarding it" << endl;
			delete(bpdu);
		} else {

			if (this->port_status[port].state != BLOCKING) {
				EV << "TCN arrived on a non-root/non-blocked port. " << endl;
				EV << "ACK the TCN and forwarding it via the root port" << endl;

				// ack the TCN
				this->sendTopologyChangeAckBPDU(port);
				// forward the TCN via the root port
				bpdu->setMessageAge(bpdu->getMessageAge()+1);
				bpdu->setPortId(this->getRootPort());
				this->sendBPDU(bpdu,this->getRootPort());
			} else {
				EV << "TCN arrived on a non-root/blocked port. discarding it" << endl;
				delete(bpdu);
			}
		}
	}

}

void MACRelayUnitSTPNP::sendConfigurationBPDU(int port_idx) {

	// get a new BPDU
	BPDU* bpdu = this->getNewBPDU(CONF_BPDU);
	// prepare the configuration BPDU
	bpdu->setPortId(port_idx);

	if (port_idx>-1) {
		// RSTP: port information
		bpdu->setPortRole(this->port_status[port_idx].role);
		bpdu->setForwarding(this->port_status[port_idx].state == FORWARDING ? true : false);
		bpdu->setLearning(this->port_status[port_idx].state == LEARNING ? true : false);

		// RSTP: set proposal flag we are proposing a transition on that port
		if (this->port_status[port_idx].proposing) {
			bpdu->setProposal(true);
			bpdu->setAgreement(false);
		}

		// RSTP: set agreement flag when we are agree (all port synced) with the root bridge info
		if (this->port_status[port_idx].agree) {
			bpdu->setProposal(false);
			bpdu->setAgreement(true);
			// use the observed_pr on the port to agree the proposal.
			// root bridge id comes already set from the getNewBPDU method
			bpdu->setSenderBID(this->port_status[port_idx].observed_pr.bridge_id);
			bpdu->setPortId(this->port_status[port_idx].observed_pr.port_id);
			bpdu->setRootPathCost(this->port_status[port_idx].observed_pr.root_path_cost);
			bpdu->setMessageAge(0);
		}
	}

	// send the BPDU
	this->sendBPDU(bpdu,port_idx);
}

void MACRelayUnitSTPNP::sendTopologyChangeNotificationBPDU(int port_idx) {
	// get a new BPDU
	BPDU* bpdu = this->getNewBPDU(TCN_BPDU);
	// prepare the configuration BPDU
	bpdu->setPortId(port_idx);
	bpdu->setMessageAge(0);
	// send the BPDU
	this->sendBPDU(bpdu,this->getRootPort());
}

void MACRelayUnitSTPNP::sendTopologyChangeAckBPDU(int port_idx) {

	// get a new BPDU
	BPDU* bpdu = this->getNewBPDU(CONF_BPDU);
	// prepare the configuration BPDU and flag it as ACK
	bpdu->setName("CBPDU+ACK");
	bpdu->setAckFlag(true);
	bpdu->setMessageAge(0);
	bpdu->setPortId(port_idx);
	// send the BPDU
	this->sendBPDU(bpdu,port_idx);
}

// send BPDU's

void MACRelayUnitSTPNP::sendBPDU(BPDU* bpdu,int port) {

	// check if we need to flag the bpdu with the Topology Change flag (only for root bridge)
	if (this->priority_vector.root_id == this->bridge_id) {
		if (simTime() < this->topology_change_timeout) {
			bpdu->setTopologyChangeFlag(true);

			EV << "flagging BPDU with TC flag" << endl;
		} else {
			EV << "TC Flag is off" << endl;
		}
	}

	if (port>-1) {

		if (!(this->port_status[port].isHoldTimerActive())) {
			// bpdu is meant to be send via the port id
			EV << "Sending " << bpdu->getName() << " via port " << port << endl;

			EtherFrame* frame = new EtherFrame(bpdu->getName());
			frame->setDest(MACRelayUnitSTPNP::STPMCAST_ADDRESS);
			frame->setSrc(this->bridge_id.address);

			BPDU* b = bpdu->dup();
			frame->encapsulate(b);
			send(frame, "lowerLayerOut", port);
			this->scheduleHoldTimer(port);

			// record the proposed bpdu on this port
			this->port_status[port].proposed_pr = PriorityVector(b->getRootBID(),b->getRootPathCost(),b->getSenderBID(),b->getPortId());

		} else {
			EV << "Hold timer active on port " << port << " canceling transmission and enqueuing the BPDU" << endl;
			this->port_status[port].BPDUQueue.insert(bpdu);
			return;
		}
	} else {
		// bpdu is an STP broadcast
		for (int i=0; i<numPorts; ++i) {
			// send the bpdu only to the designated ports
			//if (this->port_status[i].role == DESIGNATED_PORT || this->port_status[i].role == ROOT_PORT) {
			if (this->port_status[i].role == DESIGNATED_PORT) {
				if (bpdu->getArrivalGate()!=NULL) {
					if (bpdu->getArrivalGate()->getIndex()==i) {
						continue;
					}
				}
				BPDU* b = bpdu->dup();
				b->setPortId(i);
				// RSTP: set RST BPDU information
				b->setPortRole(this->port_status[i].role);
				b->setForwarding(this->port_status[i].state == FORWARDING ? true : false);
				b->setLearning(this->port_status[i].state == LEARNING ? true : false);

				// proposing can be a broadcast, but agree is always a sent on a single port
				if (this->port_status[i].proposing) {
					bpdu->setProposal(true);
					bpdu->setAgreement(false);
				}

				if (!this->port_status[i].isHoldTimerActive()) {

					EV << "Sending " << bpdu->getName() << " via port " << b->getPortId() << endl;

					EtherFrame* frame = new EtherFrame(bpdu->getName());
					frame->setDest(MACRelayUnitSTPNP::STPMCAST_ADDRESS);
					frame->setSrc(this->bridge_id.address);

					frame->encapsulate(b);
					send(frame, "lowerLayerOut", i);
					this->scheduleHoldTimer(i);

					// record the proposed bpdu on this port
					this->port_status[i].proposed_pr = PriorityVector(b->getRootBID(),b->getRootPathCost(),b->getSenderBID(),b->getPortId());

				} else {
					EV << "Hold timer active on port " << i << " canceling transmission and enqueuing the BPDU" << endl;
					this->port_status[i].BPDUQueue.insert(b);
				}
			}
		}
	}
	delete bpdu;
}

BPDU* MACRelayUnitSTPNP::getNewBPDU(BPDUType type) {
	// prepare the bpdu

	BPDU* bpdu = NULL;
	switch (type) {
		case CONF_BPDU:
			bpdu = new CBPDU("CBPDU");
			break;
		case TCN_BPDU:
			bpdu = new TCNBPDU("TCN BPDU");
			break;
	}

	bpdu->setRootBID(this->priority_vector.root_id);
	//TODO: set the cost according the link speed
	bpdu->setRootPathCost(this->priority_vector.root_path_cost+1);
	bpdu->setSenderBID(this->bridge_id);
	bpdu->setMessageAge(this->message_age+1);
	bpdu->setMaxAge(this->max_age_time);
	bpdu->setHelloTime(this->hello_time);
	bpdu->setForwardDelay(this->forward_delay);
	bpdu->setTopologyChangeFlag(false);

	return bpdu;
}

void MACRelayUnitSTPNP::broadcastFrame(EtherFrame *frame, int inputport)
{
    for (int i=0; i<numPorts; ++i)
        if (i!=inputport && this->port_status[i].state==FORWARDING)
            send((EtherFrame*)frame->dup(), "lowerLayerOut", i);
    delete frame;
}

