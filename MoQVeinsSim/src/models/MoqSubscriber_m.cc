//
// Generated file, do not edit! Created by opp_msgtool 6.3 from models/MoqSubscriber.msg.
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
#include "MoqSubscriber_m.h"

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

Register_Class(MoqSubscriber)

MoqSubscriber::MoqSubscriber() : ::inet::FieldsChunk()
{
}

MoqSubscriber::MoqSubscriber(const MoqSubscriber& other) : ::inet::FieldsChunk(other)
{
    copy(other);
}

MoqSubscriber::~MoqSubscriber()
{
}

MoqSubscriber& MoqSubscriber::operator=(const MoqSubscriber& other)
{
    if (this == &other) return *this;
    ::inet::FieldsChunk::operator=(other);
    copy(other);
    return *this;
}

void MoqSubscriber::copy(const MoqSubscriber& other)
{
    this->subscriberId = other.subscriberId;
    this->trackNamespace = other.trackNamespace;
    this->trackName = other.trackName;
    this->subscriberPriority = other.subscriberPriority;
    this->startObjectId = other.startObjectId;
}

void MoqSubscriber::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::inet::FieldsChunk::parsimPack(b);
    doParsimPacking(b,this->subscriberId);
    doParsimPacking(b,this->trackNamespace);
    doParsimPacking(b,this->trackName);
    doParsimPacking(b,this->subscriberPriority);
    doParsimPacking(b,this->startObjectId);
}

void MoqSubscriber::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::inet::FieldsChunk::parsimUnpack(b);
    doParsimUnpacking(b,this->subscriberId);
    doParsimUnpacking(b,this->trackNamespace);
    doParsimUnpacking(b,this->trackName);
    doParsimUnpacking(b,this->subscriberPriority);
    doParsimUnpacking(b,this->startObjectId);
}

long MoqSubscriber::getSubscriberId() const
{
    return this->subscriberId;
}

void MoqSubscriber::setSubscriberId(long subscriberId)
{
    handleChange();
    this->subscriberId = subscriberId;
}

const char * MoqSubscriber::getTrackNamespace() const
{
    return this->trackNamespace.c_str();
}

void MoqSubscriber::setTrackNamespace(const char * trackNamespace)
{
    handleChange();
    this->trackNamespace = trackNamespace;
}

const char * MoqSubscriber::getTrackName() const
{
    return this->trackName.c_str();
}

void MoqSubscriber::setTrackName(const char * trackName)
{
    handleChange();
    this->trackName = trackName;
}

long MoqSubscriber::getSubscriberPriority() const
{
    return this->subscriberPriority;
}

void MoqSubscriber::setSubscriberPriority(long subscriberPriority)
{
    handleChange();
    this->subscriberPriority = subscriberPriority;
}

long MoqSubscriber::getStartObjectId() const
{
    return this->startObjectId;
}

void MoqSubscriber::setStartObjectId(long startObjectId)
{
    handleChange();
    this->startObjectId = startObjectId;
}

class MoqSubscriberDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_subscriberId,
        FIELD_trackNamespace,
        FIELD_trackName,
        FIELD_subscriberPriority,
        FIELD_startObjectId,
    };
  public:
    MoqSubscriberDescriptor();
    virtual ~MoqSubscriberDescriptor();

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

Register_ClassDescriptor(MoqSubscriberDescriptor)

MoqSubscriberDescriptor::MoqSubscriberDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(moqveinssim::MoqSubscriber)), "inet::FieldsChunk")
{
    propertyNames = nullptr;
}

MoqSubscriberDescriptor::~MoqSubscriberDescriptor()
{
    delete[] propertyNames;
}

bool MoqSubscriberDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<MoqSubscriber *>(obj)!=nullptr;
}

const char **MoqSubscriberDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *MoqSubscriberDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int MoqSubscriberDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 5+base->getFieldCount() : 5;
}

unsigned int MoqSubscriberDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_subscriberId
        FD_ISEDITABLE,    // FIELD_trackNamespace
        FD_ISEDITABLE,    // FIELD_trackName
        FD_ISEDITABLE,    // FIELD_subscriberPriority
        FD_ISEDITABLE,    // FIELD_startObjectId
    };
    return (field >= 0 && field < 5) ? fieldTypeFlags[field] : 0;
}

const char *MoqSubscriberDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "subscriberId",
        "trackNamespace",
        "trackName",
        "subscriberPriority",
        "startObjectId",
    };
    return (field >= 0 && field < 5) ? fieldNames[field] : nullptr;
}

int MoqSubscriberDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "subscriberId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "trackNamespace") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "trackName") == 0) return baseIndex + 2;
    if (strcmp(fieldName, "subscriberPriority") == 0) return baseIndex + 3;
    if (strcmp(fieldName, "startObjectId") == 0) return baseIndex + 4;
    return base ? base->findField(fieldName) : -1;
}

const char *MoqSubscriberDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "long",    // FIELD_subscriberId
        "string",    // FIELD_trackNamespace
        "string",    // FIELD_trackName
        "long",    // FIELD_subscriberPriority
        "long",    // FIELD_startObjectId
    };
    return (field >= 0 && field < 5) ? fieldTypeStrings[field] : nullptr;
}

const char **MoqSubscriberDescriptor::getFieldPropertyNames(int field) const
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

const char *MoqSubscriberDescriptor::getFieldProperty(int field, const char *propertyName) const
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

int MoqSubscriberDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        default: return 0;
    }
}

void MoqSubscriberDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'MoqSubscriber'", field);
    }
}

const char *MoqSubscriberDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string MoqSubscriberDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        case FIELD_subscriberId: return long2string(pp->getSubscriberId());
        case FIELD_trackNamespace: return oppstring2string(pp->getTrackNamespace());
        case FIELD_trackName: return oppstring2string(pp->getTrackName());
        case FIELD_subscriberPriority: return long2string(pp->getSubscriberPriority());
        case FIELD_startObjectId: return long2string(pp->getStartObjectId());
        default: return "";
    }
}

void MoqSubscriberDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        case FIELD_subscriberId: pp->setSubscriberId(string2long(value)); break;
        case FIELD_trackNamespace: pp->setTrackNamespace((value)); break;
        case FIELD_trackName: pp->setTrackName((value)); break;
        case FIELD_subscriberPriority: pp->setSubscriberPriority(string2long(value)); break;
        case FIELD_startObjectId: pp->setStartObjectId(string2long(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqSubscriber'", field);
    }
}

omnetpp::cValue MoqSubscriberDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        case FIELD_subscriberId: return (omnetpp::intval_t)(pp->getSubscriberId());
        case FIELD_trackNamespace: return pp->getTrackNamespace();
        case FIELD_trackName: return pp->getTrackName();
        case FIELD_subscriberPriority: return (omnetpp::intval_t)(pp->getSubscriberPriority());
        case FIELD_startObjectId: return (omnetpp::intval_t)(pp->getStartObjectId());
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'MoqSubscriber' as cValue -- field index out of range?", field);
    }
}

void MoqSubscriberDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        case FIELD_subscriberId: pp->setSubscriberId(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_trackNamespace: pp->setTrackNamespace(value.stringValue()); break;
        case FIELD_trackName: pp->setTrackName(value.stringValue()); break;
        case FIELD_subscriberPriority: pp->setSubscriberPriority(omnetpp::checked_int_cast<long>(value.intValue())); break;
        case FIELD_startObjectId: pp->setStartObjectId(omnetpp::checked_int_cast<long>(value.intValue())); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqSubscriber'", field);
    }
}

const char *MoqSubscriberDescriptor::getFieldStructName(int field) const
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

omnetpp::any_ptr MoqSubscriberDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void MoqSubscriberDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    MoqSubscriber *pp = omnetpp::fromAnyPtr<MoqSubscriber>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'MoqSubscriber'", field);
    }
}

}  // namespace moqveinssim

namespace omnetpp {

}  // namespace omnetpp

