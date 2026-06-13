
import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;
namespace moqveinssim;

//
// Application-level acknowledgement sent by a subscriber to the relay once an
// object has been fully received and reassembled. The relay uses it to pace
// forwarding: the next object on a given (subscriber, track) stream is only
// sent after the previous object's ACK arrives. This guarantees the previous
// object is fully QUIC-ACKed (its stream send buffer is empty) before the next
// object is enqueued, so two objects never merge into a SequenceChunk.
//
class MoqObjectAck extends inet::FieldsChunk
{
    string subscriberId;
    string trackNamespace;
    string trackName;
    long objectId;
}
