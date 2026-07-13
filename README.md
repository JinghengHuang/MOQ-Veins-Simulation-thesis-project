# V2X Communication simulator for Media over QUIC


## Purporse

Test performance of MOQ in V2X communication in a V2I scenario where vehicle communicate with infrastructures through 5G network.

## Architecture

- Message Transmission:

Publisher <-> Edge-side relay <-> Subscriber

- Simulation software:
SUMO <-> Veins <-> OMNeT++

## Next Steps

Implement TCP/UDP model similar to MOQ model and compare performance between MOQ Model and TCP/UDP

✔ Experiment on simulating 5G networks between vehicles and integrate MoQ within this system

✔ Simulating 5G networks between vehicles, by integrating Veins with INET and Simu5G module
Ongoing: Integrate MoQ
Todo: Test MoQ performance

## Development

- To install environment, use `opp_env` and use following command to install related projects:

```bash
opp_env install inet-4.6.0 omnetpp-6.3.0 simu5g-git veins-git
```

- To run simulation:

Run OMNET++ with:

```bash
./StartOmnetPP.sh
```

then run veins(and sumo) with:

```bash
./VeinsDemo.sh
```

then run simulation from OMNET++ within project MoQVeinsSim (`./MoQVeinsSim/simulations/omnetpp.ini`)

## Module Configurations

All application traffic travels over 5G NR via Simu5G; Veins is used **only** as the mobility
feeder (SUMO via TraCI), so the Veins 802.11p PHY -- and with it the two-ray interference and
obstacle shadowing models -- is not on the data path and does not apply here.

Propagation is modelled by Simu5G's `NrChannelModel_3GPP38_901` (3GPP TR 38.901), which
provides LOS/NLOS path loss, log-normal shadowing and Jakes fading. It is declared explicitly
in `omnetpp.ini` rather than left to defaults.

## Scenarios

- **Urban** (default): 3x3 SUMO grid, 200m edges, 50 km/h. Configs `MOQ`, `MOQ_TCP`,
  `MOQ_UDP`, `MOQ_SW`, `MOQ_Partial`.
- **Highway**: straight 3 km, 3-lane corridor at 120 km/h, with the two gNodeBs spaced along
  it so a traversing vehicle hands over once mid-run. Same configs with an `_HW` suffix.

Both scenarios run 8 vehicles. `MOQ_Partial_MultiPub` scales the offered load by promoting
cars 0-2 to publishers.

## Network simulation

- [Veins_INET](https://veins.car2x.org/documentation/modules/#veins_inet) is a subproject (included with Veins) which allows using Veins as a mobility model in either INET 3 of INET 4, which allows using the full feature set of the INET Framework in a Veins simulation: full IPv4/IPv6 stacks, wired networking, mobile ad hoc network protocols, bit-precise PCAP traces, or network emulation -- as well as model libraries based on the INET Framework, e.g., 4G/5G cellular networking and mobile broadband technologies 3gpp LTE, C-V2x, and 5G NR-V2x (by also importing module libraries like SimuLTE or Simu5G).