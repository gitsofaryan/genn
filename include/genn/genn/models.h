#pragma once

// Standard C++ includes
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// GeNN includes
#include "snippet.h"
#include "initVarSnippet.h"
#include "varAccess.h"

// Forward declarations
class NeuronGroup;
class SynapseGroup;
class CurrentSource;
class NeuronGroupInternal;
class SynapseGroupInternal;
class CurrentSourceInternal;
namespace CodeGenerator
{
class BackendBase;
}

//----------------------------------------------------------------------------
// Macros
//----------------------------------------------------------------------------
#define DECLARE_MODEL(TYPE, NUM_PARAMS, NUM_VARS)                       \
    DECLARE_SNIPPET(TYPE, NUM_PARAMS);                                  \
    typedef Models::VarInitContainerBase<NUM_VARS> VarValues;        \
    typedef Models::VarInitContainerBase<0> PreVarValues;            \
    typedef Models::VarInitContainerBase<0> PostVarValues

#define IMPLEMENT_MODEL(TYPE) IMPLEMENT_SNIPPET(TYPE)

#define SET_VARS(...) virtual VarVec getVars() const override{ return __VA_ARGS__; }


//----------------------------------------------------------------------------
// Models::Base
//----------------------------------------------------------------------------
//! Base class for all models - in addition to the parameters snippets have, models can have state variables
namespace Models
{
class GENN_EXPORT Base : public Snippet::Base
{
public:
    //----------------------------------------------------------------------------
    // Structs
    //----------------------------------------------------------------------------
    //! A variable has a name, a type and an access type
    /*! Explicit constructors required as although, through the wonders of C++
        aggregate initialization, access would default to VarAccess::READ_WRITE
        if not specified, this results in a -Wmissing-field-initializers warning on GCC and Clang*/
    struct Var
    {
        Var(const std::string &n, const std::string &t, VarAccess a) : name(n), type(t), access(a)
        {}
        Var(const std::string &n, const std::string &t) : Var(n, t, VarAccess::READ_WRITE)
        {}
        Var() : Var("", "", VarAccess::READ_WRITE)
        {}

        bool operator == (const Var &other) const
        {
            return ((name == other.name) && (type == other.type) && (access == other.access));
        }

        std::string name;
        std::string type;
        VarAccess access;
    };

    struct VarRef
    {
        VarRef(const std::string &n, const std::string &t, VarAccessMode a) : name(n), type(t), access(a)
        {}
        VarRef(const std::string &n, const std::string &t) : VarRef(n, t, VarAccessMode::READ_WRITE)
        {}
        VarRef() : VarRef("", "", VarAccessMode::READ_WRITE)
        {}

        bool operator == (const VarRef &other) const
        {
            return ((name == other.name) && (type == other.type) && (access == other.access));
        }

        std::string name;
        std::string type;
        VarAccessMode access;
    };

    //----------------------------------------------------------------------------
    // Typedefines
    //----------------------------------------------------------------------------
    typedef std::vector<Var> VarVec;
    typedef std::vector<VarRef> VarRefVec;

    //----------------------------------------------------------------------------
    // Declared virtuals
    //------------------------------------------------------------------------
    //! Gets names and types (as strings) of model variables
    virtual VarVec getVars() const{ return {}; }

    //------------------------------------------------------------------------
    // Public methods
    //------------------------------------------------------------------------
    //! Find the index of a named variable
    size_t getVarIndex(const std::string &varName) const
    {
        return getNamedVecIndex(varName, getVars());
    }

protected:
    //------------------------------------------------------------------------
    // Protected methods
    //------------------------------------------------------------------------
    bool canBeMerged(const Base *other) const
    {
        // Return true if vars and egps match
        return (Snippet::Base::canBeMerged(other)
                && (getVars() == other->getVars()));
    }
};


//----------------------------------------------------------------------------
// Models::VarInit
//----------------------------------------------------------------------------
//! Class used to bind together everything required to initialise a variable:
//! 1. A pointer to a variable initialisation snippet
//! 2. The parameters required to control the variable initialisation snippet
class VarInit : public Snippet::Init<InitVarSnippet::Base>
{
public:
    VarInit(const InitVarSnippet::Base *snippet, const std::vector<double> &params)
        : Snippet::Init<InitVarSnippet::Base>(snippet, params)
    {
    }

    VarInit(double constant)
        : Snippet::Init<InitVarSnippet::Base>(InitVarSnippet::Constant::getInstance(), {constant})
    {
    }
};

//----------------------------------------------------------------------------
// Models::VarInitContainerBase
//----------------------------------------------------------------------------
template<size_t NumVars>
using VarInitContainerBase = Snippet::InitialiserContainerBase<VarInit, NumVars>;

//----------------------------------------------------------------------------
// Models::VarReferenceBase
//----------------------------------------------------------------------------
class VarReferenceBase
{
public:
    //------------------------------------------------------------------------
    // Public API
    //------------------------------------------------------------------------
    const Models::Base::Var &getVar() const { return m_Var; }
    size_t getVarIndex() const { return m_VarIndex; }

protected:
    VarReferenceBase(size_t varIndex, const Models::Base::VarVec &varVec)
    : m_VarIndex(varIndex), m_Var(varVec.at(varIndex))
    {}

private:
    //------------------------------------------------------------------------
    // Members
    //------------------------------------------------------------------------
    const size_t m_VarIndex;
    const Models::Base::Var m_Var;
};

//----------------------------------------------------------------------------
// Models::VarReference
//----------------------------------------------------------------------------
class VarReference : public VarReferenceBase
{
public:
    //------------------------------------------------------------------------
    // Public API
    //------------------------------------------------------------------------
    unsigned int getSize() const { return m_Size; }
    const std::string &getTargetName() const { return m_GetTargetNameFn(); }

    //------------------------------------------------------------------------
    // Static API
    //------------------------------------------------------------------------
    static VarReference createVarRef(const NeuronGroup *ng, const std::string &varName);
    static VarReference createVarRef(const CurrentSource *cs, const std::string &varName);
    static VarReference createPSMVarRef(const SynapseGroup *sg, const std::string &varName);
    static VarReference createWUPreVarRef(const SynapseGroup *sg, const std::string &varName);
    static VarReference createWUPostVarRef(const SynapseGroup *sg, const std::string &varName);
    
private:
    //------------------------------------------------------------------------
    // Enumerations
    //------------------------------------------------------------------------
    typedef std::function<const std::string &(void)> GetTargetNameFn;

    VarReference(const NeuronGroupInternal *ng, const std::string &varName);
    VarReference(const CurrentSourceInternal *cs, const std::string &varName);
    VarReference(GetTargetNameFn getTargetNameFn, unsigned int size, 
                 size_t varIndex, const Models::Base::VarVec &varVec);

    //------------------------------------------------------------------------
    // Members
    //------------------------------------------------------------------------
    const unsigned int m_Size;
    GetTargetNameFn m_GetTargetNameFn;
};

//----------------------------------------------------------------------------
// Models::VarReferenceContainerBase
//----------------------------------------------------------------------------
template<size_t NumVars>
using VarReferenceContainerBase = Snippet::InitialiserContainerBase<VarReference, NumVars>;

//----------------------------------------------------------------------------
// Models::WUVarReference
//----------------------------------------------------------------------------
class WUVarReference : public VarReferenceBase
{
public:
    WUVarReference(const SynapseGroup *sg, const std::string &varName);

    //------------------------------------------------------------------------
    // Public API
    //------------------------------------------------------------------------
    const SynapseGroup *getSynapseGroup() const;

private:
    //------------------------------------------------------------------------
    // Members
    //------------------------------------------------------------------------
    const SynapseGroupInternal *m_SG;
};

//----------------------------------------------------------------------------
// Models::WUVarReferenceContainerBase
//----------------------------------------------------------------------------
template<size_t NumVars>
using WUVarReferenceContainerBase = Snippet::InitialiserContainerBase<WUVarReference, NumVars>;
} // Models
