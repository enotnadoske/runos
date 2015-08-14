/*
 * Copyright 2015 Applied Research Center for Computer Networks
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "STP.hh"

#include "Topology.hh"

REGISTER_APPLICATION(STP, {"switch-manager", "link-discovery", "topology", ""})

void SwitchSTP::computeSTP()
{
    parent->computePathForSwitch(this->sw->id());
}

void SwitchSTP::resetBroadcast()
{
    for (auto port : ports) {
        if (port.second->to_switch)
            unsetBroadcast(port.second->port_no);
    }
}

void SwitchSTP::setSwitchPort(uint32_t port_no, uint64_t dpid)
{
    ports.at(port_no)->to_switch = true;
    ports.at(port_no)->nextSwitch = parent->switch_list[dpid];
}

void STP::init(Loader* loader, const Config& config)
{    
    QObject* ld = ILinkDiscovery::get(loader);

    connect(ld, SIGNAL(linkDiscovered(switch_and_port, switch_and_port)),
                     this, SLOT(onLinkDiscovered(switch_and_port, switch_and_port)));
    connect(ld, SIGNAL(linkBroken(switch_and_port, switch_and_port)),
                     this, SLOT(onLinkBroken(switch_and_port, switch_and_port)));

    SwitchManager* sw = SwitchManager::get(loader);
    connect(sw, &SwitchManager::switchDiscovered, this, &STP::onSwitchDiscovered);
    connect(sw, &SwitchManager::switchDown, this, &STP::onSwitchDown);

    topo = Topology::get(loader);
}

STPPorts STP::getSTP(uint64_t dpid)
{
    std::vector<uint32_t> ports;
    if (switch_list.count(dpid) == 0) {
        return ports;
    }

    SwitchSTP* sw = switch_list[dpid];    
    if (!sw->computed) {
        return ports;
    }

    for (auto port : sw->ports) {
        if (port.second->broadcast)
            ports.push_back(port.second->port_no);
    }
    return ports;
}

void STP::onLinkDiscovered(switch_and_port from, switch_and_port to)
{
    if (switch_list.count(from.dpid) == 0)
        return;

    if (switch_list.count(to.dpid) == 0)
        return;

    SwitchSTP* sw = switch_list[from.dpid];
    if (!sw->existsPort(from.port)) {
        Port* port = new Port(from.port);
        sw->ports[from.port] = port;
    }
    if (!sw->root)
        sw->unsetBroadcast(from.port);
    sw->setSwitchPort(from.port, to.dpid);

    sw = switch_list[to.dpid];
    if (!sw->existsPort(to.port)) {
        Port* port = new Port(to.port);
        sw->ports[to.port] = port;
    }
    if (!sw->root)
        sw->unsetBroadcast(to.port);
    sw->setSwitchPort(to.port, from.dpid);

    // recompute pathes for all switches
    for (auto ss : switch_list) {
        if (!ss.second->root)
            ss.second->computed = false;
    }
}

void STP::onLinkBroken(switch_and_port from, switch_and_port to)
{
    // recompute pathes for all switches
    for (auto ss : switch_list) {
        if (!ss.second->root)
            ss.second->computed = false;
    }
}

void STP::onSwitchDiscovered(Switch* dp)
{
    SwitchSTP* sw;
    if (switch_list.empty())
        sw = new SwitchSTP(dp, this, true, true);
    else
        sw = new SwitchSTP(dp, this);

    switch_list[dp->id()] = sw;

    connect(dp, &Switch::portUp, this, &STP::onPortUp);
    connect(sw->timer, SIGNAL(timeout()), sw, SLOT(computeSTP()));
    sw->timer->start(POLL_TIMEOUT * 1000);
}

void STP::onSwitchDown(Switch* dp)
{
    if (switch_list.count(dp->id()) > 0) {
        SwitchSTP* sw = switch_list[dp->id()];
        sw->timer->stop();
        switch_list.erase(dp->id());
        delete sw;
    }
}

void STP::onPortUp(Switch *dp, of13::Port port)
{
    if (switch_list.count(dp->id()) > 0) {
        SwitchSTP* sw = switch_list[dp->id()];
        if ( !sw->existsPort(port.port_no()) && port.port_no() < of13::OFPP_MAX ) {
            Port* p = new Port(port.port_no());
            sw->ports[port.port_no()] = p;
        }
    }
}

SwitchSTP* STP::findRoot()
{
    for (auto it : switch_list) {
        if (it.second->root)
            return it.second;
    }
    return nullptr;
}

void STP::computePathForSwitch(uint64_t dpid)
{
    static std::mutex compute;
    if (!switch_list[dpid]->computed) {
        SwitchSTP* root = findRoot();
        if (root == nullptr) {
            LOG(ERROR) << "Root switch not found!";
            SwitchSTP* sw = switch_list[dpid];
            sw->root = true;
            sw->computed = true;
            return;
        }

        SwitchSTP* sw = switch_list[dpid];
        std::vector<uint32_t> old_broadcast = getSTP(dpid);

        compute.lock();
        sw->resetBroadcast();

        data_link_route route = topo->computeRoute(dpid, root->sw->id());
        if (route.size() > 0) {
            uint32_t broadcast_port = route[0].port;

            if (sw->existsPort(broadcast_port))
                sw->setBroadcast(broadcast_port);

            sw->nextSwitchToRoot = switch_list[route[1].dpid];

            // getting broadcast port on second switch
            data_link_route r_route = topo->computeRoute(route[1].dpid, dpid);
            SwitchSTP* r_sw = switch_list[r_route[0].dpid];
            uint32_t r_broadcast_port = r_route[0].port;
            if (r_sw->existsPort(r_broadcast_port))
                r_sw->setBroadcast(r_broadcast_port);

            for (auto port : sw->ports) {
                if (port.second->to_switch) {
                    if (port.second->nextSwitch->nextSwitchToRoot == sw) {
                        sw->setBroadcast(port.second->port_no);
                    }
                }
            }

            if (getSTP(dpid).size() == old_broadcast.size())
                sw->computed = true;

        } else {
            LOG(WARNING) << "Path between " << FORMAT_DPID << dpid
                << " and root switch " << FORMAT_DPID << root->sw->id() << " not found";
        }
        compute.unlock();
    }
}
