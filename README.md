# V2X Communication simulator for Media over QUIC


## Purporse

Test performance of MOQ in V2X communication in a V2I scenario where vehicle communicate with infrastructures through 5G network.

## Architecture

- Message Transmission:

Publisher <-> Edge-side relay <-> Subscriber

- Simulation software:
SUMO <-> Veins <-> OMNeT++

## Next Steps

Experiment on simulating 5G networks between vehicles and integrate MoQ within this system

## Development

- To simplify development difficulties, implemented directly upon the base demo project provided by Veins, which simulates a traffic congestion scenario in Erlangen.

## Module Configurations

- For more precise simulation on signal path loss, uses two ray inference model as suggested in the [document](https://veins.car2x.org/documentation/modules/#tworay).
- Uses simple obstacle shadowing model to simulate signal losses caused by building obstruction, as suggested in the [document](https://veins.car2x.org/documentation/modules/#obstacles).

## Network simulation

- [Veins_INET](https://veins.car2x.org/documentation/modules/#veins_inet) is a subproject (included with Veins) which allows using Veins as a mobility model in either INET 3 of INET 4, which allows using the full feature set of the INET Framework in a Veins simulation: full IPv4/IPv6 stacks, wired networking, mobile ad hoc network protocols, bit-precise PCAP traces, or network emulation -- as well as model libraries based on the INET Framework, e.g., 4G/5G cellular networking and mobile broadband technologies 3gpp LTE, C-V2x, and 5G NR-V2x (by also importing module libraries like SimuLTE or Simu5G).