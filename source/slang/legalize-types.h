// legalize-types.h
#ifndef SLANG_LEGALIZE_TYPES_H_INCLUDED
#define SLANG_LEGALIZE_TYPES_H_INCLUDED

// This file and `legalize-types.cpp` implement the core
// logic for taking a `Type` as produced by the front-end,
// and turning it into a suitable representation for use
// on a particular back-end.
//
// The main work applies to aggregate (e.g., `struct`) types,
// since various targets have rules about what is and isn't
// allowed in an aggregate (or where aggregates are allowed
// to be used).
//
// We might completely replace an aggregate `Type` with a
// "pseudo-type" that is just the enumeration of its field
// types (sort of a tuple type) so that a variable declared
// with the original type should be transformed into a
// bunch of individual variables.
//
// Alternatively, we might replace an aggregate type, where
// only *some* of the fields are illegal with a combination
// of an aggregate (containing the legal/legalized fields),
// and some extra tuple-ified fields.

#include "../core/basic.h"
#include "ir-insts.h"
#include "syntax.h"
#include "type-layout.h"
#include "name.h"

namespace Slang
{

struct IRBuilder;

struct LegalTypeImpl : RefObject
{
};
struct ImplicitDerefType;
struct TuplePseudoType;
struct PairPseudoType;
struct PairInfo;
struct LegalElementWrapping;
struct WrappedBufferPseudoType;

    /// A flavor for types or values that arise during legalization.
enum class LegalFlavor
{
        /// Nothing: an empty type or value. Equivalent to `void`.
    none,

        /// A simple type/value that can be represented as an `IRType*` or `IRInst*`
    simple,

        /// Logically, a pointer-like type/value, but represented as the type/value being pointed type.
    implicitDeref,

        /// A compound type/value made up of the constituent fields of some original value.
    tuple,

        /// A type/value that was split into "ordinary" and "special" parts.
    pair,

        /// A type/value that represents, e.g., `ConstantBuffer<T>` where `T` needed legalization.
    wrappedBuffer,
};

struct LegalType
{
    typedef LegalFlavor Flavor;

    Flavor              flavor = Flavor::none;
    RefPtr<RefObject>   obj;
    IRType*             irType;

    static LegalType simple(IRType* type)
    {
        LegalType result;
        result.flavor = Flavor::simple;
        result.irType = type;
        return result;
    }

    IRType* getSimple() const
    {
        SLANG_ASSERT(flavor == Flavor::simple);
        return irType;
    }

    static LegalType implicitDeref(
        LegalType const& valueType);

    RefPtr<ImplicitDerefType> getImplicitDeref() const
    {
        SLANG_ASSERT(flavor == Flavor::implicitDeref);
        return obj.as<ImplicitDerefType>();
    }

    static LegalType tuple(
        RefPtr<TuplePseudoType>   tupleType);

    RefPtr<TuplePseudoType> getTuple() const
    {
        SLANG_ASSERT(flavor == Flavor::tuple);
        return obj.as<TuplePseudoType>();
    }

    static LegalType pair(
        RefPtr<PairPseudoType> pairType);

    static LegalType pair(
        LegalType const&    ordinaryType,
        LegalType const&    specialType,
        RefPtr<PairInfo>    pairInfo);

    RefPtr<PairPseudoType> getPair() const
    {
        SLANG_ASSERT(flavor == Flavor::pair);
        return obj.as<PairPseudoType>();
    }

    static LegalType makeWrappedBuffer(
        IRType*             simpleType,
        LegalElementWrapping const&   elementInfo);

    RefPtr<WrappedBufferPseudoType> getWrappedBuffer() const
    {
        SLANG_ASSERT(flavor == Flavor::wrappedBuffer);
        return obj.as<WrappedBufferPseudoType>();
    }
};

struct LegalElementWrappingObj : RefObject
{
};

struct SimpleLegalElementWrappingObj;
struct ImplicitDerefLegalElementWrappingObj;
struct PairLegalElementWrappingObj;
struct TupleLegalElementWrappingObj;

    /// Information on how the element type of a buffer needs to be wrapped.
struct LegalElementWrapping
{
    typedef LegalFlavor Flavor;

    Flavor flavor;
    RefPtr<LegalElementWrappingObj> obj;

    static LegalElementWrapping makeVoid();
    static LegalElementWrapping makeSimple(IRStructKey* key, IRType* type);
    static LegalElementWrapping makeImplicitDeref(LegalElementWrapping const& field);
    static LegalElementWrapping makePair(
        LegalElementWrapping const& ordinary,
        LegalElementWrapping const& special,
        PairInfo* pairInfo);
    static LegalElementWrapping makeTuple(TupleLegalElementWrappingObj* obj);

    RefPtr<SimpleLegalElementWrappingObj> getSimple() const;
    RefPtr<ImplicitDerefLegalElementWrappingObj> getImplicitDeref() const;
    RefPtr<PairLegalElementWrappingObj> getPair() const;
    RefPtr<TupleLegalElementWrappingObj> getTuple() const;
};

struct SimpleLegalElementWrappingObj : LegalElementWrappingObj
{
    IRStructKey*    key;
    IRType*         type;
};

struct ImplicitDerefLegalElementWrappingObj : LegalElementWrappingObj
{
    LegalElementWrapping field;
};

struct PairLegalElementWrappingObj : LegalElementWrappingObj
{
    LegalElementWrapping ordinary;
    LegalElementWrapping special;
    RefPtr<PairInfo> pairInfo;
};

struct TupleLegalElementWrappingObj : LegalElementWrappingObj
{
    struct Element
    {
        IRStructKey*    key;
        LegalElementWrapping      field;
    };

    List<Element> elements;
};

// Represents the pseudo-type of a type that is pointer-like
// (and thus requires dereferencing, even if implicit), but
// was legalized to just use the type of the pointed-type value.
//
// The two cases where this comes up are:
//
//  1. When we have a type like `ConstantBuffer<Texture2D>` that
//  implies a level of indirection, but need to legalize it to just
//  `Texture2D`, which eliminates that indirection.
//
//  2. When we have a type like `ExistentialBox<Foo>` that will
//  become just a `Foo` field, but which needs to be allocated
//  out-of-line from the rest of its enclosing type.
//
struct ImplicitDerefType : LegalTypeImpl
{
    LegalType valueType;
};

// Represents the pseudo-type for a compound type
// that had to be broken apart because it contained
// one or more fields of types that shouldn't be
// allowed in aggregates.
//
// A tuple pseduo-type will have an element for
// each field of the original type, that represents
// the legalization of that field's type.
//
// It optionally also contains an "ordinary" type
// that packs together any per-field data that
// itself has (or contains) an ordinary type.
struct TuplePseudoType : LegalTypeImpl
{
    // Represents one element of the tuple pseudo-type
    struct Element
    {
        // The field that this element replaces
        IRStructKey*    key;

        // The legalized type of the element
        LegalType       type;
    };

    // All of the elements of the tuple pseduo-type.
    List<Element>   elements;
};

struct IRStructKey;

struct PairInfo : RefObject
{
    typedef unsigned int Flags;
    enum
    {
        kFlag_hasOrdinary = 0x1,
        kFlag_hasSpecial = 0x2,
    };


    struct Element
    {
        // The original field the element represents
        IRStructKey* key;

        // The conceptual type of the field.
        // If both the `hasOrdinary` and
        // `hasSpecial` bits are set, then
        // this is expected to be a
        // `LegalType::Flavor::pair`
        LegalType   type;

        // Is the value represented on
        // the ordinary side, the special
        // side, or both?
        Flags       flags;

        // If the type of this element is
        // itself a pair type (that is,
        // it both `hasOrdinary` and `hasSpecial`)
        // then this is the `PairInfo` for that
        // pair type:
        RefPtr<PairInfo> fieldPairInfo;
    };

    // For a pair type or value, we need to track
    // which fields are on which side(s).
    List<Element> elements;

    Element* findElement(IRStructKey* key)
    {
        for (auto& ee : elements)
        {
            if(ee.key == key)
                return &ee;
        }
        return nullptr;
    }
};

struct PairPseudoType : LegalTypeImpl
{
    // Any field(s) with ordinary types will
    // get captured here, usually as a single
    // `simple` or `implicitDeref` type.
    LegalType ordinaryType;

    // Any fields with "special" (not ordinary)
    // types will get captured here (usually
    // with a tuple).
    LegalType specialType;

    // The `pairInfo` field helps to tell us which members
    // of the original aggregate type appear on which side(s)
    // of the new pair type.
    RefPtr<PairInfo> pairInfo;
};


struct WrappedBufferPseudoType : LegalTypeImpl
{
    // The actual IR type that was used for the buffer.
    IRType*     simpleType;

    // Adjustments that need to be made when fetching
    // an element from this buffer type.
    //
    LegalElementWrapping  elementInfo;
};

//

RefPtr<TypeLayout> getDerefTypeLayout(
    TypeLayout* typeLayout);

RefPtr<VarLayout> getFieldLayout(
    TypeLayout*     typeLayout,
    String const&   mangledFieldName);

// Represents the "chain" of declarations that
// were followed to get to a variable that we
// are now declaring as a leaf variable.
struct LegalVarChain
{
    LegalVarChain*  next;
    VarLayout*      varLayout;
};

RefPtr<VarLayout> createVarLayout(
    LegalVarChain*  varChain,
    TypeLayout*     typeLayout);

//
// The result of legalizing an IR value will be
// represented with the `LegalVal` type. It is exposed
// in this header (rather than kept as an implementation
// detail, because the AST-based legalization logic needs
// a way to find the post-legalization version of a
// global name).
//
// TODO: We really shouldn't have this structure exposed,
// and instead should really be constructing AST-side
// `LegalExpr` values on-demand whenever we legalize something
// in the IR that will need to be used by the AST, and then
// store *those* in a map indexed in mangled names.
//

struct LegalValImpl : RefObject
{
};
struct TuplePseudoVal;
struct PairPseudoVal;
struct WrappedBufferPseudoVal;

struct LegalVal
{
    typedef LegalFlavor Flavor;

    Flavor              flavor = Flavor::none;
    RefPtr<RefObject>   obj;
    IRInst*             irValue = nullptr;

    static LegalVal simple(IRInst* irValue)
    {
        LegalVal result;
        result.flavor = Flavor::simple;
        result.irValue = irValue;
        return result;
    }

    IRInst* getSimple() const
    {
        SLANG_ASSERT(flavor == Flavor::simple);
        return irValue;
    }

    static LegalVal tuple(RefPtr<TuplePseudoVal> tupleVal);

    RefPtr<TuplePseudoVal> getTuple() const
    {
        SLANG_ASSERT(flavor == Flavor::tuple);
        return obj.as<TuplePseudoVal>();
    }

    static LegalVal implicitDeref(LegalVal const& val);
    LegalVal getImplicitDeref();

    static LegalVal pair(RefPtr<PairPseudoVal> pairInfo);
    static LegalVal pair(
        LegalVal const&     ordinaryVal,
        LegalVal const&     specialVal,
        RefPtr<PairInfo>    pairInfo);

    RefPtr<PairPseudoVal> getPair() const
    {
        SLANG_ASSERT(flavor == Flavor::pair);
        return obj.as<PairPseudoVal>();
    }
};

struct TuplePseudoVal : LegalValImpl
{
    struct Element
    {
        IRStructKey*    key;
        LegalVal        val;
    };

    List<Element>   elements;
};

struct PairPseudoVal : LegalValImpl
{
    LegalVal ordinaryVal;
    LegalVal specialVal;

    // The info to tell us which fields
    // are on which side(s)
    RefPtr<PairInfo>  pairInfo;
};

struct ImplicitDerefVal : LegalValImpl
{
    LegalVal val;
};

//

    /// Context that drives type legalization
    ///
    /// This type is an abstract base class, and there are
    /// customization points that a concrete pass needs to
    /// override (e.g., to specify what needs to be legalized).
struct IRTypeLegalizationContext
{
    Session*    session;
    IRModule*   module;
    IRBuilder*  builder;

    SharedIRBuilder sharedBuilderStorage;
    IRBuilder builderStorage;

    IRTypeLegalizationContext(
        IRModule* inModule);

    // When inserting new globals, put them before this one.
    IRInst* insertBeforeGlobal = nullptr;

    // When inserting new parameters, put them before this one.
    IRParam* insertBeforeParam = nullptr;

    Dictionary<IRInst*, LegalVal> mapValToLegalVal;

    IRVar* insertBeforeLocalVar = nullptr;

    // store instructions that have been replaced here, so we can free them
    // when legalization has done
    List<IRInst*> replacedInstructions;

    Dictionary<IRType*, LegalType> mapTypeToLegalType;

    IRBuilder* getBuilder() { return builder; }

        /// Customization point to decide what types are "special."
        ///
        /// When legalizing a `struct` type, any fields that have "special"
        /// types will get moved out of the `struc` itself.
    virtual bool isSpecialType(IRType* type) = 0;

        /// Customization point to construct uniform-buffer/block types.
        ///
        /// This function will only be called if `legalElementType` is
        /// somehow non-trivial.
        ///
    virtual LegalType createLegalUniformBufferType(
        IROp        op,
        LegalType   legalElementType) = 0;
};

// This typedef exists to support pre-existing code from when
// `IRTypeLegalizationContext` and `TypeLegalizationContext` were
// two different types that had to coordinate.
typedef struct IRTypeLegalizationContext TypeLegalizationContext;

LegalType legalizeType(
    TypeLegalizationContext*    context,
    IRType*                     type);

/// Try to find the module that (recursively) contains a given declaration.
ModuleDecl* findModuleForDecl(
    Decl*   decl);

    /// Create a uniform buffer type suitable for resource legalization.
    ///
    /// This will allocate a real buffer for the ordinary data (if any),
    /// and leave the special data (if any) as a tuple.
    ///
LegalType createLegalUniformBufferTypeForResources(
    TypeLegalizationContext*    context,
    IROp                        op,
    LegalType                   legalElementType);

    /// Create a uniform buffer type suitable for existential legalization.
    ///
    /// This will allocate a real uniform buffer for *all* the data, by
    /// declaring an intermediate `struct` type to hold the ordinary and
    /// special (existential-box) fields, if required.
    ///
LegalType createLegalUniformBufferTypeForExistentials(
    TypeLegalizationContext*    context,
    IROp                        op,
    LegalType                   legalElementType);




void legalizeExistentialTypeLayout(
    IRModule*       module,
    DiagnosticSink* sink);

void legalizeResourceTypes(
    IRModule*       module,
    DiagnosticSink* sink);

bool isResourceType(IRType* type);


}

#endif
