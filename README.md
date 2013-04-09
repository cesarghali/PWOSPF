PWOSPF (Pee-Wee OSPF)
=====================

This project involves building advanced functionality on top of the <a href="https://github.com/cesarghali/SimpleRouter" target="_new">SimpleRouter</a> project. The goal is to develop a simple dynamic routing protocol, PWOSPF, so that your router can generate its forwarding table automatically based on routes advertised by other routers on the network. By the end of this project, your router is expected to be able to build its forwarding table from link-state advertisements sent from other routers, and route traffic through complex topologies containing multiple nodes.

PWOSPF
------
The routing protocol you will be implementing is a link state protocol that is loosely based on OSPFv2. You may find a full specification of PWOSPF <a href="" target="_new">here</a>. Note that while PWOSPF is based on OSPFv2 it is sufficiently different that referring to the OSPFv2 as a reference will not be of much help and contrarily may confuse or mislead you.

Network Topology
----------------
![Network Topology]()

The task is to implement PWOSPF within your existing router so that your router will be able to do the following:

* build the correct forwarding tables on the assignment topology.
* detect when routers join/or leave the topology and correct the forwarding tables correctly.
* inter-operate with a third party reference solution that implements pwosp.

Running Multiple Routers
------------------------
Since this project requires multiple instances of your router to run simultaneously you will want to use the -r and the -v command line options. -r allows you to specify the routing table file you want to use (e.g. -r rtable.vhost1) and -v allows you to specify the host you want to connect to on the topology (e.g. -v vhost3). Connecting to vhost3 on topology 300 should look something like:

> ./sr -t 300 -v vhost3 -r rtable.vhost3

For more information check Stanford <a href="http://yuba.stanford.edu/vns/assignments/pwospf/" target="_new">Virtual Network System</a>.

Partners
--------
Wassim Itani
Ahmad El Hajj

References
----------
Ayman Kayssi, American University of Beirut, <a href="http://staff.aub.edu.lb/~ayman/" target="_new">More</a>.