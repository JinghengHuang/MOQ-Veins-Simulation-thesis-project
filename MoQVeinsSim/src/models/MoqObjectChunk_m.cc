//
// Generated file, do not edit! Created by opp_msgtool 6.3 from models/MoqObjectChunk.msg.
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
#include "MoqObjectChunk_m.h"

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

Register_Class(MoqObjectChunk)

MoqObjectChunk::MoqObjectChunk() : ::inet::FieldsChunk()
{
}

MoqObjectChunk::MoqObjectChunk(const MoqObjectChunk& other) : ::inet::FieldsChunk(other)
{
    copy(other);
}

MoqObjectChunk::~MoqObjectChunk()
{
}

MoqObjectChunk& MoqObjectChunk::operator=(const MoqObjectChunk& other)
{
    if (this == &other) return *this;
    ::inet::FieldsChunk::operator=(other);
    copy(other);
    return *this;
}

void MoqObjectChunk::copy(const MoqObjectChunk& other)
{
    this->trackId = other.trackId;
    this->trackAlias = other.trackAlias;
    this->groupId = other.groupId;
    this->objectId = other.objectId;
    this->priority = other.priority;
    this->payloadLength = other.payloadLength;
    this->creationTime = other.creationTime;
}

void MoqObjectChunk::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::inet::FieldsChunk::parsimPack(b);
    doParsimPacking(b,this->trackId);
    doParsimPacking(b,this->trackAlias);
    doParsimPacking(b,this->groupId);
    doParsimPacking(b,this->objectId);
    doParsimPacking(b,this->priority);
    doParsimPacking(b,this->payloadLength);
    doParsimPacking(b,this->creationTime);
}

void MoqObjectChunk::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::inet::FieldsChunk::parsimUnpack(b);
    doParsimUnpacking(b,this->trackId);
    doParsimUnpacking(b,this->trackAlias);
    doParsimUnpacking(b,this->groupId);
    doParsimUnpacking(b,this->objectId);
    doParsimUnpacking(b,this->priority);
    doParsimUnpacking(b,this->payloadLength);
    doParsimUnpacking(b,this->creationTime);
}

long MoqObjectChunk::getTrackId() const
{
    return this->trackId;
}

void MoqObjectChunk::setTrackId(long trackId)
{
    handleChange();
    this->trackId = trackId;
}

const char * MoqObjectChunk::getTrackAlias() const
{
    return this->trackAlias.c_str();
}

void MoqObjectChunk::setTrackAlias(const char * trackAlias)
{
    handleChange();
    this->trackAlias = trackAlias;
}

long MoqObjectChunk::getGroupId() const
{
    return this->groupId;
}

void MoqObjectChunk::setGroupId(long groupId)
{
    handleChange();
    this->groupId = groupId;
}

long MoqObjectChunk::getObjectId() const
{
    return this->objectId;
}

void MoqObjectChunk::setObjectId(long objectId)
{
    handleChange();
    this->objectId = objectId;
}

long MoqObjectChunk::getPriority() const
{
    return this->priority;
}

void MoqObjectChunk::setPriority(long priority)
{
    handleChange();
    this->priority = priority;
}

long MoqObjectChunk::getPayloadLength() const
{
    return this->payloadLength;
}

void MoqObjectChunk::setPayloadLength(long payloadLength)
{
    handleChange();
    this->payloadLength = payloadLength;
}

::omnetpp::simtime_t MoqObjectChunk::getCreationTime() const
{
    return this->creationTime;
}

void MoqObjectChunk::setCreationTime(::omnetpp::simtime_t creationTime)
{
    handleChange();
    this->creationTime = creationTime;
}

class MoqObjectChunkDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_trackId,
        FIELD_trackAlias,
        FIELD_groupId,
        FIELD_objectId,
        FIELD_priority,
        FIELD_payloadLength,
        FIELD_creationTime,
    };
  public:
    MoqObjectChunkDescriptor();
    virtual ~MoqObjectChunkDescriptor();

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

Register_ClassDescriptor(MoqObjectChunkDescriptor)

MoqObjectChunkDescriptor::MoqObjectChunkDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(moqveinssim::MoqObjectChunk)), "inet::FieldsChunk")
{
    propertyNames = nullptr;
}

MoqObjectChunkDescriptor::~MoqObjectChunkDescriptor()
{
    delete[] propertyNames;
}

bool MoqObjectChunkDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<MoqObjectChunk *>(obj)!=nullptr;
}

const char **MoqObjectChunkDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *MoqObjectChunkDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int MoqObjectChunkDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 7+base->getFieldCount() : 7;
}

unsigned int MoqObjectChunkDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_trackId
        FD_ISEDITABLE,    // FIELD_trackAlias
        FD_ISEDITABLE,    // FIELD_groupId
        FD_ISEDITABLE,    // FIELD_objectId
        FD_ISEDITABLE,    // FIELD_priority
        FD_ISEDITABLE,    // FIELD_payloadLength
        FD_ISEDITABLE,    // FIELD_creationTime
    };
    return (field >= 0 && field < 7) ? fieldTypeFlags[field] : 0;
}

const char *MoqObjectChunkDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "trackId",
        "trackAlias",
        "groupId",
        "objectId",
        "priority",
        "payloadLength",
        "creationTime",
    };
    return (field >= 0 && field < 7) ? fieldNames[field] : nullptr;
}

int MoqObjectChunkDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "trackId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "trackAlias") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "groupId") == 0) return baseIndex + 2;
    if (strcmp(fieldName, "objectId") == 0) return baseIndex + 3;
    if (strcmp(fieldName, "priority") == 0) return baseIndex + 4;
    if (strcmp(fieldName, "payloadLength") == 0) return baseIndex + 5;
    if (strcmp(fieldName, "creationTime") == 0) return baseIndex + 6;
    return base ? base->findField(fieldName) : -1;
}

const char *MoqObjectChunkDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "long",    // FIELD_trackId
        "string",    // FIELD_trackAlias
        "long",    // FIELD_groupId
        "long",    // FIELD_objectId
        "long",    // FIELD_priority
        "long",    // FIELD_payloadLength
        "omnetpp::simtime_t",    // FIELD_creationTime
    };
    return (field >= 0 && field < 7) ? fieldTypeStrings[field] : nullptr;
}

const char **MoqObjectChunkDescriptor::getFieldPropertyNames(int field) const
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

const char *MoqObjectChunkDescriptor::getFieldProperty(int field, const char *propertyName) const
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

int MoqObjectChunkDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        default: return 0;
    }
}

void MoqObjectChunkDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'MoqObjectChunk'", field);
    }
}

const char *MoqObjectChunkDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string MoqObjectChunkDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        case FIELD_trackId: return long2string(pp->getTrackId());
        case FIELD_trackAlias: return oppstring2string(pp->getTrackAlias());
        case FIELD_groupId: return long2string(pp->getGroupId());
        case FIELD_objectId: return long2string(pp->getObjectId());
        case FIELD_priority: return long2string(pp->getPriority());
        case FIELD_payloadLength: return long2string(pp->getPayloadLength());
        case FIELD_creationTime: return simtime2string(pp->getCreationTime());
        default: return "";
    }
}

void MoqObjectChunkDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        case FIELD_trackId: pp->setTrackId(string2long(value)); break;
        case FIELD_trackAlias: pp->setTrackAlias((value)); break;
        case FIELD_groupId: pp->setGroupId(string2long(value)); break;
        case FIELD_objectId: pp->setObjectId(string2long(value)); break;
        case FIELD_priority: pp->setPriority(string2long(value)); break;
        case FIELD_payloadLength: pp->setPayloadLength(string2long(value)); break;
        case FIELD_creationTime: pp->setCreationTime(string2simtime(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqObjectChunk'", field);
    }
}

omnetpp::cValue MoqObjectChunkDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        case FIELD_trackId: return (omnetpp::intval_t)(pp->getTrackId());
        case FIELD_trackAlias: return pp->getTrackAlias();
        case FIELD_groupId: return (omnetpp::intval_t)(pp->getGroupId());
        case FIELD_objectId: return (omnetpp::intval_t)(pp->getObjectId());
        case FIELD_priority: return (omnetpp::intval_t)(pp->getPriority());
        case FIELD_payloadLength: return (omnetpp::intval_t)(pp->getPayloadLength());
        case FIELD_creationTime: return pp->getCreationTime().dbl();
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'MoqObjectChunk' as cValue -- field index out of range?", field);
    }
}

void MoqObjectChunkDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        case FIELD_trackId: pp->setTrackId(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_trackAlias: pp->setTrackAlias(value.stringValue()); break;
        case FIELD_groupId: pp->setGroupId(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_objectId: pp->setObjectId(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_priority: pp->setPriority(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_payloadLength: pp->setPayloadLength(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_creationTime: pp->setCreationTime(value.doubleValue()); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqObjectChunk'", field);
    }
}

const char *MoqObjectChunkDescriptor::getFieldStructName(int field) const
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

omnetpp::any_ptr MoqObjectChunkDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void MoqObjectChunkDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqObjectChunk *pp = omnetpp::fromAnyPtr<MoqObjectChunk>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqObjectChunk'", field);
    }
}

}  // namespace moqveinssim

namespace omnetpp {

}  // namespace omnetpp

