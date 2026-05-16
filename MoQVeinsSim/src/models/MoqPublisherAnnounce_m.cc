//
// Generated file, do not edit! Created by opp_msgtool 6.3 from models/MoqPublisherAnnounce.msg.
//

// Disable warnings about unused variables, empty switch stmts, etc:
#ifdef _MSC_VER
#  pragma warning(disable:4101)
#  pragma warning(disable:4065)
#endif

#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wshadow"
#  pragma clang diagnostic ignored "-Wconversion"
#  pragma clang diagnostic ignored "-Wunused-parameter"
#  pragma clang diagnostic ignored "-Wc++98-compat"
#  pragma clang diagnostic ignored "-Wunreachable-code-break"
#  pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wconversion"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wold-style-cast"
#  pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#  pragma GCC diagnostic ignored "-Wfloat-conversion"
#endif

#include <iostream>
#include <sstream>
#include <memory>
#include <type_traits>
#include "MoqPublisherAnnounce_m.h"

namespace omnetpp {

// Template pack/unpack rules. They are declared *after* a1l type-specific pack functions for multiple reasons.
// They are in the omnetpp namespace, to allow them to be found by argument-dependent lookup via the cCommBuffer argument

// Packing/unpacking an std::vector
template<typename T, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::vector<T,A>& v)
{
    int n = v.size();
    doParsimPacking(buffer, n);
    for (int i = 0; i < n; i++)
        doParsimPacking(buffer, v[i]);
}

template<typename T, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::vector<T,A>& v)
{
    int n;
    doParsimUnpacking(buffer, n);
    v.resize(n);
    for (int i = 0; i < n; i++)
        doParsimUnpacking(buffer, v[i]);
}

// Packing/unpacking an std::list
template<typename T, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::list<T,A>& l)
{
    doParsimPacking(buffer, (int)l.size());
    for (typename std::list<T,A>::const_iterator it = l.begin(); it != l.end(); ++it)
        doParsimPacking(buffer, (T&)*it);
}

template<typename T, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::list<T,A>& l)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        l.push_back(T());
        doParsimUnpacking(buffer, l.back());
    }
}

// Packing/unpacking an std::set
template<typename T, typename Tr, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::set<T,Tr,A>& s)
{
    doParsimPacking(buffer, (int)s.size());
    for (typename std::set<T,Tr,A>::const_iterator it = s.begin(); it != s.end(); ++it)
        doParsimPacking(buffer, *it);
}

template<typename T, typename Tr, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::set<T,Tr,A>& s)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        T x;
        doParsimUnpacking(buffer, x);
        s.insert(x);
    }
}

// Packing/unpacking an std::map
template<typename K, typename V, typename Tr, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::map<K,V,Tr,A>& m)
{
    doParsimPacking(buffer, (int)m.size());
    for (typename std::map<K,V,Tr,A>::const_iterator it = m.begin(); it != m.end(); ++it) {
        doParsimPacking(buffer, it->first);
        doParsimPacking(buffer, it->second);
    }
}

template<typename K, typename V, typename Tr, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::map<K,V,Tr,A>& m)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        K k; V v;
        doParsimUnpacking(buffer, k);
        doParsimUnpacking(buffer, v);
        m[k] = v;
    }
}

// Default pack/unpack function for arrays
template<typename T>
void doParsimArrayPacking(omnetpp::cCommBuffer *b, const T *t, int n)
{
    for (int i = 0; i < n; i++)
        doParsimPacking(b, t[i]);
}

template<typename T>
void doParsimArrayUnpacking(omnetpp::cCommBuffer *b, T *t, int n)
{
    for (int i = 0; i < n; i++)
        doParsimUnpacking(b, t[i]);
}

// Default rule to prevent compiler from choosing base class' doParsimPacking() function
template<typename T>
void doParsimPacking(omnetpp::cCommBuffer *, const T& t)
{
    throw omnetpp::cRuntimeError("Parsim error: No doParsimPacking() function for type %s", omnetpp::opp_typename(typeid(t)));
}

template<typename T>
void doParsimUnpacking(omnetpp::cCommBuffer *, T& t)
{
    throw omnetpp::cRuntimeError("Parsim error: No doParsimUnpacking() function for type %s", omnetpp::opp_typename(typeid(t)));
}

}  // namespace omnetpp

namespace moqveinssim {

Register_Class(MoqPublisherAnnounce)

MoqPublisherAnnounce::MoqPublisherAnnounce() : ::inet::FieldsChunk()
{
}

MoqPublisherAnnounce::MoqPublisherAnnounce(const MoqPublisherAnnounce& other) : ::inet::FieldsChunk(other)
{
    copy(other);
}

MoqPublisherAnnounce::~MoqPublisherAnnounce()
{
}

MoqPublisherAnnounce& MoqPublisherAnnounce::operator=(const MoqPublisherAnnounce& other)
{
    if (this == &other) return *this;
    ::inet::FieldsChunk::operator=(other);
    copy(other);
    return *this;
}

void MoqPublisherAnnounce::copy(const MoqPublisherAnnounce& other)
{
    this->publisherId = other.publisherId;
    this->trackId = other.trackId;
    this->trackNamespace = other.trackNamespace;
    this->trackName = other.trackName;
    this->trackAlias = other.trackAlias;
    this->objectType = other.objectType;
    this->payloadSize = other.payloadSize;
    this->sendInterval = other.sendInterval;
    this->priority = other.priority;
}

void MoqPublisherAnnounce::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::inet::FieldsChunk::parsimPack(b);
    doParsimPacking(b,this->publisherId);
    doParsimPacking(b,this->trackId);
    doParsimPacking(b,this->trackNamespace);
    doParsimPacking(b,this->trackName);
    doParsimPacking(b,this->trackAlias);
    doParsimPacking(b,this->objectType);
    doParsimPacking(b,this->payloadSize);
    doParsimPacking(b,this->sendInterval);
    doParsimPacking(b,this->priority);
}

void MoqPublisherAnnounce::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::inet::FieldsChunk::parsimUnpack(b);
    doParsimUnpacking(b,this->publisherId);
    doParsimUnpacking(b,this->trackId);
    doParsimUnpacking(b,this->trackNamespace);
    doParsimUnpacking(b,this->trackName);
    doParsimUnpacking(b,this->trackAlias);
    doParsimUnpacking(b,this->objectType);
    doParsimUnpacking(b,this->payloadSize);
    doParsimUnpacking(b,this->sendInterval);
    doParsimUnpacking(b,this->priority);
}

long MoqPublisherAnnounce::getPublisherId() const
{
    return this->publisherId;
}

void MoqPublisherAnnounce::setPublisherId(long publisherId)
{
    handleChange();
    this->publisherId = publisherId;
}

long MoqPublisherAnnounce::getTrackId() const
{
    return this->trackId;
}

void MoqPublisherAnnounce::setTrackId(long trackId)
{
    handleChange();
    this->trackId = trackId;
}

const char * MoqPublisherAnnounce::getTrackNamespace() const
{
    return this->trackNamespace.c_str();
}

void MoqPublisherAnnounce::setTrackNamespace(const char * trackNamespace)
{
    handleChange();
    this->trackNamespace = trackNamespace;
}

const char * MoqPublisherAnnounce::getTrackName() const
{
    return this->trackName.c_str();
}

void MoqPublisherAnnounce::setTrackName(const char * trackName)
{
    handleChange();
    this->trackName = trackName;
}

const char * MoqPublisherAnnounce::getTrackAlias() const
{
    return this->trackAlias.c_str();
}

void MoqPublisherAnnounce::setTrackAlias(const char * trackAlias)
{
    handleChange();
    this->trackAlias = trackAlias;
}

const char * MoqPublisherAnnounce::getObjectType() const
{
    return this->objectType.c_str();
}

void MoqPublisherAnnounce::setObjectType(const char * objectType)
{
    handleChange();
    this->objectType = objectType;
}

long MoqPublisherAnnounce::getPayloadSize() const
{
    return this->payloadSize;
}

void MoqPublisherAnnounce::setPayloadSize(long payloadSize)
{
    handleChange();
    this->payloadSize = payloadSize;
}

::omnetpp::simtime_t MoqPublisherAnnounce::getSendInterval() const
{
    return this->sendInterval;
}

void MoqPublisherAnnounce::setSendInterval(::omnetpp::simtime_t sendInterval)
{
    handleChange();
    this->sendInterval = sendInterval;
}

long MoqPublisherAnnounce::getPriority() const
{
    return this->priority;
}

void MoqPublisherAnnounce::setPriority(long priority)
{
    handleChange();
    this->priority = priority;
}

class MoqPublisherAnnounceDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_publisherId,
        FIELD_trackId,
        FIELD_trackNamespace,
        FIELD_trackName,
        FIELD_trackAlias,
        FIELD_objectType,
        FIELD_payloadSize,
        FIELD_sendInterval,
        FIELD_priority,
    };
  public:
    MoqPublisherAnnounceDescriptor();
    virtual ~MoqPublisherAnnounceDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(MoqPublisherAnnounceDescriptor)

MoqPublisherAnnounceDescriptor::MoqPublisherAnnounceDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(moqveinssim::MoqPublisherAnnounce)), "inet::FieldsChunk")
{
    propertyNames = nullptr;
}

MoqPublisherAnnounceDescriptor::~MoqPublisherAnnounceDescriptor()
{
    delete[] propertyNames;
}

bool MoqPublisherAnnounceDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<MoqPublisherAnnounce *>(obj)!=nullptr;
}

const char **MoqPublisherAnnounceDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *MoqPublisherAnnounceDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int MoqPublisherAnnounceDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 9+base->getFieldCount() : 9;
}

unsigned int MoqPublisherAnnounceDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_publisherId
        FD_ISEDITABLE,    // FIELD_trackId
        FD_ISEDITABLE,    // FIELD_trackNamespace
        FD_ISEDITABLE,    // FIELD_trackName
        FD_ISEDITABLE,    // FIELD_trackAlias
        FD_ISEDITABLE,    // FIELD_objectType
        FD_ISEDITABLE,    // FIELD_payloadSize
        FD_ISEDITABLE,    // FIELD_sendInterval
        FD_ISEDITABLE,    // FIELD_priority
    };
    return (field >= 0 && field < 9) ? fieldTypeFlags[field] : 0;
}

const char *MoqPublisherAnnounceDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "publisherId",
        "trackId",
        "trackNamespace",
        "trackName",
        "trackAlias",
        "objectType",
        "payloadSize",
        "sendInterval",
        "priority",
    };
    return (field >= 0 && field < 9) ? fieldNames[field] : nullptr;
}

int MoqPublisherAnnounceDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "publisherId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "trackId") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "trackNamespace") == 0) return baseIndex + 2;
    if (strcmp(fieldName, "trackName") == 0) return baseIndex + 3;
    if (strcmp(fieldName, "trackAlias") == 0) return baseIndex + 4;
    if (strcmp(fieldName, "objectType") == 0) return baseIndex + 5;
    if (strcmp(fieldName, "payloadSize") == 0) return baseIndex + 6;
    if (strcmp(fieldName, "sendInterval") == 0) return baseIndex + 7;
    if (strcmp(fieldName, "priority") == 0) return baseIndex + 8;
    return base ? base->findField(fieldName) : -1;
}

const char *MoqPublisherAnnounceDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "long",    // FIELD_publisherId
        "long",    // FIELD_trackId
        "string",    // FIELD_trackNamespace
        "string",    // FIELD_trackName
        "string",    // FIELD_trackAlias
        "string",    // FIELD_objectType
        "long",    // FIELD_payloadSize
        "omnetpp::simtime_t",    // FIELD_sendInterval
        "long",    // FIELD_priority
    };
    return (field >= 0 && field < 9) ? fieldTypeStrings[field] : nullptr;
}

const char **MoqPublisherAnnounceDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *MoqPublisherAnnounceDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int MoqPublisherAnnounceDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        default: return 0;
    }
}

void MoqPublisherAnnounceDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'MoqPublisherAnnounce'", field);
    }
}

const char *MoqPublisherAnnounceDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string MoqPublisherAnnounceDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        case FIELD_publisherId: return long2string(pp->getPublisherId());
        case FIELD_trackId: return long2string(pp->getTrackId());
        case FIELD_trackNamespace: return oppstring2string(pp->getTrackNamespace());
        case FIELD_trackName: return oppstring2string(pp->getTrackName());
        case FIELD_trackAlias: return oppstring2string(pp->getTrackAlias());
        case FIELD_objectType: return oppstring2string(pp->getObjectType());
        case FIELD_payloadSize: return long2string(pp->getPayloadSize());
        case FIELD_sendInterval: return simtime2string(pp->getSendInterval());
        case FIELD_priority: return long2string(pp->getPriority());
        default: return "";
    }
}

void MoqPublisherAnnounceDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        case FIELD_publisherId: pp->setPublisherId(string2long(value)); break;
        case FIELD_trackId: pp->setTrackId(string2long(value)); break;
        case FIELD_trackNamespace: pp->setTrackNamespace((value)); break;
        case FIELD_trackName: pp->setTrackName((value)); break;
        case FIELD_trackAlias: pp->setTrackAlias((value)); break;
        case FIELD_objectType: pp->setObjectType((value)); break;
        case FIELD_payloadSize: pp->setPayloadSize(string2long(value)); break;
        case FIELD_sendInterval: pp->setSendInterval(string2simtime(value)); break;
        case FIELD_priority: pp->setPriority(string2long(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqPublisherAnnounce'", field);
    }
}

omnetpp::cValue MoqPublisherAnnounceDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        case FIELD_publisherId: return (omnetpp::intval_t)(pp->getPublisherId());
        case FIELD_trackId: return (omnetpp::intval_t)(pp->getTrackId());
        case FIELD_trackNamespace: return pp->getTrackNamespace();
        case FIELD_trackName: return pp->getTrackName();
        case FIELD_trackAlias: return pp->getTrackAlias();
        case FIELD_objectType: return pp->getObjectType();
        case FIELD_payloadSize: return (omnetpp::intval_t)(pp->getPayloadSize());
        case FIELD_sendInterval: return pp->getSendInterval().dbl();
        case FIELD_priority: return (omnetpp::intval_t)(pp->getPriority());
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'MoqPublisherAnnounce' as cValue -- field index out of range?", field);
    }
}

void MoqPublisherAnnounceDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        case FIELD_publisherId: pp->setPublisherId(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_trackId: pp->setTrackId(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_trackNamespace: pp->setTrackNamespace(value.stringValue()); break;
        case FIELD_trackName: pp->setTrackName(value.stringValue()); break;
        case FIELD_trackAlias: pp->setTrackAlias(value.stringValue()); break;
        case FIELD_objectType: pp->setObjectType(value.stringValue()); break;
        case FIELD_payloadSize: pp->setPayloadSize(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_sendInterval: pp->setSendInterval(value.doubleValue()); break;
        case FIELD_priority: pp->setPriority(omnetpp::checked_int_cast<long>(value.intValue())); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqPublisherAnnounce'", field);
    }
}

const char *MoqPublisherAnnounceDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr MoqPublisherAnnounceDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void MoqPublisherAnnounceDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqPublisherAnnounce *pp = omnetpp::fromAnyPtr<MoqPublisherAnnounce>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqPublisherAnnounce'", field);
    }
}

}  // namespace moqveinssim

namespace omnetpp {

}  // namespace omnetpp

