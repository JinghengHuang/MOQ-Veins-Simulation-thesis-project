//
//                  Simu5G
//
// Authors: Giovanni Nardini, Giovanni Stea, Antonio Virdis (University of Pisa)
//
// This file is part of a software released under the license included in file
// "license.pdf". Please read LICENSE and README files before using it.
// The above files and the present reference are part of the software itself,
// and cannot be removed from it.
//


package moqveinssim.nodes;

import inet.transportlayer.quic.Quic;
import simu5g.nodes.NrCar;

import org.car2x.veins.base.modules.*;

//
// Defines a LteCar node as an NR User Equipment (UE) with vehicular-like
// mobility. The latter is modeled by utilizing the Veins-Inet project.
//
// See https://veins.car2x.org/documentation/modules/#veins_inet
//
module QuicNrCar extends NrCar
{ 
    parameters:
        bool hasQuic = default(true);

    submodules:
        quic: Quic if hasQuic {
            parameters:
                @display("p=600,300");
        }

    connections allowunconnected:
        if hasQuic {
            // app/transport dispatcher <-> quic
            at.out++ --> quic.appIn;
            quic.appOut --> at.in++;

            // quic <-> udp
            quic.udpOut --> at.in++;
            at.out++ --> quic.udpIn;
        }
}
