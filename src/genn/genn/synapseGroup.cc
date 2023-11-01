#include "synapseGroup.h"

// Standard includes
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

// GeNN includes
#include "gennUtils.h"
#include "neuronGroupInternal.h"
#include "synapseGroupInternal.h"
#include "type.h"

// ------------------------------------------------------------------------
// GeNN::SynapseGroup
// ------------------------------------------------------------------------
namespace GeNN
{
void SynapseGroup::setWUVarLocation(const std::string &varName, VarLocation loc)
{
    m_WUVarLocation[getWUModel()->getVarIndex(varName)] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setWUPreVarLocation(const std::string &varName, VarLocation loc)
{
    m_WUPreVarLocation[getWUModel()->getPreVarIndex(varName)] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setWUPostVarLocation(const std::string &varName, VarLocation loc)
{
    m_WUPostVarLocation[getWUModel()->getPostVarIndex(varName)] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setWUExtraGlobalParamLocation(const std::string &paramName, VarLocation loc)
{
    m_WUExtraGlobalParamLocation[getWUModel()->getExtraGlobalParamIndex(paramName)] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setPSVarLocation(const std::string &varName, VarLocation loc)
{
    m_PSVarLocation[getPSModel()->getVarIndex(varName)] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setPostTargetVar(const std::string &varName)
{
    // If varname is either 'ISyn' or name of target neuron group additional input variable, store
    const auto additionalInputVars = getTrgNeuronGroup()->getNeuronModel()->getAdditionalInputVars();
    if(varName == "Isyn" || 
       std::find_if(additionalInputVars.cbegin(), additionalInputVars.cend(), 
                    [&varName](const Models::Base::ParamVal &v){ return (v.name == varName); }) != additionalInputVars.cend())
    {
        m_PSTargetVar = varName;
    }
    else {
        throw std::runtime_error("Target neuron group has no input variable '" + varName + "'");
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setPreTargetVar(const std::string &varName)
{
    // If varname is either 'ISyn' or name of a presynaptic neuron group additional input variable, store
    const auto additionalInputVars = getSrcNeuronGroup()->getNeuronModel()->getAdditionalInputVars();
    if(varName == "Isyn" || 
       std::find_if(additionalInputVars.cbegin(), additionalInputVars.cend(), 
                    [&varName](const Models::Base::ParamVal &v){ return (v.name == varName); }) != additionalInputVars.cend())
    {
        m_PreTargetVar = varName;
    }
    else {
        throw std::runtime_error("Presynaptic neuron group has no input variable '" + varName + "'");
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setPSExtraGlobalParamLocation(const std::string &paramName, VarLocation loc)
{
    m_PSExtraGlobalParamLocation[getPSModel()->getExtraGlobalParamIndex(paramName)] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setSparseConnectivityExtraGlobalParamLocation(const std::string &paramName, VarLocation loc)
{
    m_ConnectivityExtraGlobalParamLocation[m_SparseConnectivityInitialiser.getSnippet()->getExtraGlobalParamIndex(paramName)] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setSparseConnectivityLocation(VarLocation loc)
{ 
    m_SparseConnectivityLocation = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setMaxConnections(unsigned int maxConnections)
{
    if(getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
        // If sparse connectivity initialiser provides a function to calculate max row length
        auto calcMaxRowLengthFunc = m_SparseConnectivityInitialiser.getSnippet()->getCalcMaxRowLengthFunc();
        if(calcMaxRowLengthFunc) {
            // Call function and if max connections we specify is less than the bound imposed by the snippet, give error
            auto connectivityMaxRowLength = calcMaxRowLengthFunc(getSrcNeuronGroup()->getNumNeurons(), getTrgNeuronGroup()->getNumNeurons(),
                                                                 m_SparseConnectivityInitialiser.getParams());
            if (maxConnections < connectivityMaxRowLength) {
                throw std::runtime_error("setMaxConnections: max connections must be higher than that already specified by sparse connectivity initialisation snippet.");
            }
        }

        m_MaxConnections = maxConnections;
    }
    else if(getMatrixType() & SynapseMatrixConnectivity::TOEPLITZ) {
        throw std::runtime_error("setMaxConnections: Synapse group already has max connections defined by toeplitz connectivity initialisation snippet.");
    }
    else {
        throw std::runtime_error("setMaxConnections: Synapse group is densely connected. Setting max connections is not required in this case.");
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setMaxSourceConnections(unsigned int maxConnections)
{
    if(getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
        // If sparse connectivity initialiser provides a function to calculate max col length
        auto calcMaxColLengthFunc = m_SparseConnectivityInitialiser.getSnippet()->getCalcMaxColLengthFunc();
        if (calcMaxColLengthFunc) {
            // Call function and if max connections we specify is less than the bound imposed by the snippet, give error
            auto connectivityMaxColLength = calcMaxColLengthFunc(getSrcNeuronGroup()->getNumNeurons(), getTrgNeuronGroup()->getNumNeurons(),
                                                                 m_SparseConnectivityInitialiser.getParams());
            if (maxConnections < connectivityMaxColLength) {
                throw std::runtime_error("setMaxSourceConnections: max source connections must be higher than that already specified by sparse connectivity initialisation snippet.");
            }
        }

        m_MaxSourceConnections = maxConnections;
    }
    else {
        throw std::runtime_error("setMaxSourceConnections: Synapse group is densely connected. Setting max connections is not required in this case.");
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setMaxDendriticDelayTimesteps(unsigned int maxDendriticDelayTimesteps)
{
    // **TODO** constraints on this
    m_MaxDendriticDelayTimesteps = maxDendriticDelayTimesteps;
}
//----------------------------------------------------------------------------
void SynapseGroup::setSpanType(SpanType spanType)
{
    if ((getMatrixType() & SynapseMatrixConnectivity::SPARSE) || (getMatrixType() & SynapseMatrixConnectivity::PROCEDURAL)) {
        m_SpanType = spanType;
    }
    else {
        throw std::runtime_error("setSpanType: This function can only be used on synapse groups with sparse or bitmask connectivity.");
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setNumThreadsPerSpike(unsigned int numThreadsPerSpike)
{
    if (m_SpanType == SpanType::PRESYNAPTIC) {
        m_NumThreadsPerSpike = numThreadsPerSpike;
    }
    else {
        throw std::runtime_error("setNumThreadsPerSpike: This function can only be used on synapse groups with a presynaptic span type.");
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setBackPropDelaySteps(unsigned int timesteps)
{
    m_BackPropDelaySteps = timesteps;

    m_TrgNeuronGroup->checkNumDelaySlots(m_BackPropDelaySteps);
}
//----------------------------------------------------------------------------
void SynapseGroup::setNarrowSparseIndEnabled(bool enabled)
{
    if(getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
        m_NarrowSparseIndEnabled = enabled;
    }
    else {
        throw std::runtime_error("setNarrowSparseIndEnabled: This function can only be used on synapse groups with sparse connectivity.");
    }
}
//----------------------------------------------------------------------------
unsigned int SynapseGroup::getMaxConnections() const
{ 
    return m_MaxConnections; 
}
//----------------------------------------------------------------------------
unsigned int SynapseGroup::getMaxSourceConnections() const
{ 
    return m_MaxSourceConnections;
}
//----------------------------------------------------------------------------
size_t SynapseGroup::getKernelSizeFlattened() const
{
    return std::accumulate(getKernelSize().cbegin(), getKernelSize().cend(), 1, std::multiplies<unsigned int>());
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getSparseConnectivityLocation() const
{ 
    return m_SparseConnectivityLocation;
}
//----------------------------------------------------------------------------
bool SynapseGroup::isTrueSpikeRequired() const
{
    return !Utils::areTokensEmpty(getWUSimCodeTokens());
}
//----------------------------------------------------------------------------
bool SynapseGroup::isSpikeEventRequired() const
{
     return !Utils::areTokensEmpty(getWUEventCodeTokens());
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPreSpikeTimeRequired() const
{
    return isPreTimeReferenced("st_pre");
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPreSpikeEventTimeRequired() const
{
    return isPreTimeReferenced("set_pre");
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPrevPreSpikeTimeRequired() const
{
    return isPreTimeReferenced("prev_st_pre");
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPrevPreSpikeEventTimeRequired() const
{
    return isPreTimeReferenced("prev_set_pre");
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPostSpikeTimeRequired() const
{
    return isPostTimeReferenced("st_post");
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPrevPostSpikeTimeRequired() const
{
    return isPostTimeReferenced("prev_st_post");
}
//----------------------------------------------------------------------------
bool SynapseGroup::isZeroCopyEnabled() const
{
    // If there are any postsynaptic variables implemented in zero-copy mode return true
    if(std::any_of(m_PSVarLocation.begin(), m_PSVarLocation.end(),
        [](VarLocation loc){ return (loc & VarLocation::ZERO_COPY); }))
    {
        return true;
    }

    // If there are any weight update variables implemented in zero-copy mode return true
    if(std::any_of(m_WUVarLocation.begin(), m_WUVarLocation.end(),
        [](VarLocation loc){ return (loc & VarLocation::ZERO_COPY); }))
    {
        return true;
    }

    // If there are any weight update variables implemented in zero-copy mode return true
    if(std::any_of(m_WUPreVarLocation.begin(), m_WUPreVarLocation.end(),
        [](VarLocation loc){ return (loc & VarLocation::ZERO_COPY); }))
    {
        return true;
    }

    // If there are any weight update variables implemented in zero-copy mode return true
    if(std::any_of(m_WUPostVarLocation.begin(), m_WUPostVarLocation.end(),
        [](VarLocation loc){ return (loc & VarLocation::ZERO_COPY); }))
    {
        return true;
    }

    return false;
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getWUVarLocation(const std::string &var) const
{
    return m_WUVarLocation[getWUModel()->getVarIndex(var)];
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getWUPreVarLocation(const std::string &var) const
{
    return m_WUPreVarLocation[getWUModel()->getPreVarIndex(var)];
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getWUPostVarLocation(const std::string &var) const
{
    return m_WUPostVarLocation[getWUModel()->getPostVarIndex(var)];
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getWUExtraGlobalParamLocation(const std::string &paramName) const
{
    return m_WUExtraGlobalParamLocation[getWUModel()->getExtraGlobalParamIndex(paramName)];
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getPSVarLocation(const std::string &var) const
{
    return m_PSVarLocation[getPSModel()->getVarIndex(var)];
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getPSExtraGlobalParamLocation(const std::string &paramName) const
{
    return m_PSExtraGlobalParamLocation[getPSModel()->getExtraGlobalParamIndex(paramName)];
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getSparseConnectivityExtraGlobalParamLocation(const std::string &paramName) const
{
    return m_ConnectivityExtraGlobalParamLocation[m_SparseConnectivityInitialiser.getSnippet()->getExtraGlobalParamIndex(paramName)];
}
//----------------------------------------------------------------------------
SynapseGroup::SynapseGroup(const std::string &name, SynapseMatrixType matrixType, unsigned int delaySteps,
                           const WeightUpdateModels::Base *wu, const std::unordered_map<std::string, double> &wuParams, const std::unordered_map<std::string, InitVarSnippet::Init> &wuVarInitialisers, 
                           const std::unordered_map<std::string, InitVarSnippet::Init> &wuPreVarInitialisers, const std::unordered_map<std::string, InitVarSnippet::Init> &wuPostVarInitialisers,
                           const std::unordered_map<std::string, Models::VarReference> &wuPreNeuronVarReferences, const std::unordered_map<std::string, Models::VarReference> &wuPostNeuronVarReferences,
                           const PostsynapticModels::Base *ps, const std::unordered_map<std::string, double> &psParams, const std::unordered_map<std::string, InitVarSnippet::Init> &psVarInitialisers, 
                           const std::unordered_map<std::string, Models::VarReference> &psNeuronVarReferences,
                           NeuronGroupInternal *srcNeuronGroup, NeuronGroupInternal *trgNeuronGroup,
                           const InitSparseConnectivitySnippet::Init &connectivityInitialiser,
                           const InitToeplitzConnectivitySnippet::Init &toeplitzInitialiser,
                           VarLocation defaultVarLocation, VarLocation defaultExtraGlobalParamLocation,
                           VarLocation defaultSparseConnectivityLocation, bool defaultNarrowSparseIndEnabled)
    :   m_Name(name), m_SpanType(SpanType::POSTSYNAPTIC), m_NumThreadsPerSpike(1), m_DelaySteps(delaySteps), m_BackPropDelaySteps(0),
        m_MaxDendriticDelayTimesteps(1), m_MatrixType(matrixType),  m_SrcNeuronGroup(srcNeuronGroup), m_TrgNeuronGroup(trgNeuronGroup), 
        m_EventThresholdReTestRequired(false), m_NarrowSparseIndEnabled(defaultNarrowSparseIndEnabled),
        m_InSynLocation(defaultVarLocation),  m_DendriticDelayLocation(defaultVarLocation),
        m_WUModel(wu), m_WUParams(wuParams), m_WUVarInitialisers(wuVarInitialisers), m_WUPreVarInitialisers(wuPreVarInitialisers), 
        m_WUPostVarInitialisers(wuPostVarInitialisers), m_WUPreNeuronVarReferences(wuPreNeuronVarReferences), m_WUPostNeuronVarReferences(wuPostNeuronVarReferences),
        m_PSModel(ps), m_PSParams(psParams), m_PSVarInitialisers(psVarInitialisers), m_PSNeuronVarReferences(psNeuronVarReferences),
        m_WUVarLocation(wuVarInitialisers.size(), defaultVarLocation), m_WUPreVarLocation(wuPreVarInitialisers.size(), defaultVarLocation),
        m_WUPostVarLocation(wuPostVarInitialisers.size(), defaultVarLocation), m_WUExtraGlobalParamLocation(wu->getExtraGlobalParams().size(), defaultExtraGlobalParamLocation),
        m_PSVarLocation(psVarInitialisers.size(), defaultVarLocation), m_PSExtraGlobalParamLocation(ps->getExtraGlobalParams().size(), defaultExtraGlobalParamLocation),
        m_SparseConnectivityInitialiser(connectivityInitialiser), m_ToeplitzConnectivityInitialiser(toeplitzInitialiser), m_SparseConnectivityLocation(defaultSparseConnectivityLocation), 
        m_ConnectivityExtraGlobalParamLocation(connectivityInitialiser.getSnippet()->getExtraGlobalParams().size(), defaultExtraGlobalParamLocation), 
        m_FusedPSTarget(nullptr), m_FusedWUPreTarget(nullptr), m_FusedWUPostTarget(nullptr), m_FusedPreOutputTarget(nullptr), m_PSTargetVar("Isyn"), m_PreTargetVar("Isyn")
{
    // Validate names
    Utils::validatePopName(name, "Synapse group");
    getWUModel()->validate(getWUParams(), getWUVarInitialisers(), 
                           getWUPreVarInitialisers(), getWUPostVarInitialisers(),
                           getWUPreNeuronVarReferences(), getWUPostNeuronVarReferences(),
                           "Synapse group " + getName() + " weight update model ");
    getPSModel()->validate(getPSParams(), getPSVarInitialisers(), getPSNeuronVarReferences(),
                           "Synapse group " + getName() + " postsynaptic model ");

    // Check variable reference types
    Models::checkVarReferenceTypes(getPSNeuronVarReferences(), getPSModel()->getNeuronVarRefs());
    Models::checkVarReferenceTypes(getWUPreNeuronVarReferences(), getWUModel()->getPreNeuronVarRefs());
    Models::checkVarReferenceTypes(getWUPostNeuronVarReferences(), getWUModel()->getPostNeuronVarRefs());
    
    // Check additional local variable reference constraints
    Models::checkLocalVarReferences(getPSNeuronVarReferences(), getPSModel()->getNeuronVarRefs(),
                                    getTrgNeuronGroup(), "Postsynaptic model variable references can only point to postsynaptic neuron group.");
    Models::checkLocalVarReferences(getWUPreNeuronVarReferences(), getWUModel()->getPreNeuronVarRefs(),
                                    getSrcNeuronGroup(), "Weight update model presynaptic variable references can only point to presynaptic neuron group.");
    Models::checkLocalVarReferences(getWUPostNeuronVarReferences(), getWUModel()->getPostNeuronVarRefs(),
                                    getTrgNeuronGroup(), "Weight update model postsynaptic variable references can only point to postsynaptic neuron group.");
    
     // Scan weight update model code strings
    m_WUSimCodeTokens = Utils::scanCode(
        getWUModel()->getSimCode(), "Synapse group '" + getName() + "' weight update model sim code");
    m_WUEventCodeTokens = Utils::scanCode(
        getWUModel()->getEventCode(), "Synapse group '" + getName() + "' weight update model event code");
    m_WUPostLearnCodeTokens = Utils::scanCode(
        getWUModel()->getLearnPostCode(), "Synapse group '" + getName() + "' weight update model learn post code");
    m_WUSynapseDynamicsCodeTokens = Utils::scanCode(
        getWUModel()->getSynapseDynamicsCode(), "Synapse group '" + getName() + "' weight update model synapse dynamics code");
    m_WUEventThresholdCodeTokens = Utils::scanCode(
        getWUModel()->getEventThresholdConditionCode(), "Synapse group '" + getName() + "' weight update model event threshold code");
    m_WUPreSpikeCodeTokens = Utils::scanCode(
        getWUModel()->getPreSpikeCode(), "Synapse group '" + getName() + "' weight update model pre spike code");
    m_WUPostSpikeCodeTokens = Utils::scanCode(
        getWUModel()->getPostSpikeCode(), "Synapse group '" + getName() + "' weight update model post spike code");
    m_WUPreDynamicsCodeTokens = Utils::scanCode(
        getWUModel()->getPreDynamicsCode(), "Synapse group '" + getName() + "' weight update model pre dynamics code");
    m_WUPostDynamicsCodeTokens = Utils::scanCode(
        getWUModel()->getPostDynamicsCode(), "Synapse group '" + getName() + "' weight update model post dynamics code");
    
    // Scan postsynaptic update model code strings
    m_PSApplyInputCodeTokens = Utils::scanCode(
        getPSModel()->getApplyInputCode(), "Synapse group '" + getName() + "' postsynaptic update model apply input code");
    m_PSDecayCodeTokens = Utils::scanCode(
        getPSModel()->getDecayCode(), "Synapse group '" + getName() + "' postsynaptic update model decay code");

    // If connectivity is procedural
    if(m_MatrixType & SynapseMatrixConnectivity::PROCEDURAL) {
        // If there's a toeplitz initialiser, give an error
        if(!Utils::areTokensEmpty(m_ToeplitzConnectivityInitialiser.getDiagonalBuildCodeTokens())) {
            throw std::runtime_error("Cannot use procedural connectivity with toeplitz initialisation snippet");
        }

        // If there's no row build code, give an error
        if(Utils::areTokensEmpty(m_SparseConnectivityInitialiser.getRowBuildCodeTokens())) {
            throw std::runtime_error("Cannot use procedural connectivity without specifying a connectivity initialisation snippet with row building code");
        }

        // If there's column build code, give an error
        if(!Utils::areTokensEmpty(m_SparseConnectivityInitialiser.getColBuildCodeTokens())) {
            throw std::runtime_error("Cannot use procedural connectivity with connectivity initialisation snippets with column building code");
        }

        // If the weight update model has code for postsynaptic-spike triggered updating, give an error
        if(!Utils::areTokensEmpty(m_WUPostLearnCodeTokens)) {
            throw std::runtime_error("Procedural connectivity cannot be used for synapse groups with postsynaptic spike-triggered learning");
        }

        // If weight update model has code for continuous synapse dynamics, give error
        // **THINK** this would actually be pretty trivial to implement
        if (!Utils::areTokensEmpty(m_WUSynapseDynamicsCodeTokens)) {
            throw std::runtime_error("Procedural connectivity cannot be used for synapse groups with continuous synapse dynamics");
        }
    }
    // Otherwise, if WEIGHTS are procedural e.g. in the case of DENSE_PROCEDURALG, give error if RNG is required for weights
    else if(m_MatrixType & SynapseMatrixWeight::PROCEDURAL) {
        if(Utils::isRNGRequired(m_WUVarInitialisers)) {
            throw std::runtime_error("Procedural weights used without procedural connectivity cannot currently access RNG.");
        }
    }
    
    // If synapse group has Toeplitz connectivity
    if(m_MatrixType & SynapseMatrixConnectivity::TOEPLITZ) {
        // Give an error if there is sparse connectivity initialiser code
        if(!Utils::areTokensEmpty(m_SparseConnectivityInitialiser.getRowBuildCodeTokens()) 
           || !Utils::areTokensEmpty(m_SparseConnectivityInitialiser.getColBuildCodeTokens())) 
        {
            throw std::runtime_error("Cannot use TOEPLITZ connectivity with sparse connectivity initialisation snippet.");
        }

        // Give an error if there isn't toeplitz connectivity initialiser code
        if(Utils::areTokensEmpty(m_ToeplitzConnectivityInitialiser.getDiagonalBuildCodeTokens())) {
            throw std::runtime_error("TOEPLITZ connectivity requires toeplitz connectivity initialisation snippet.");
        }

        // Give an error if connectivity initialisation snippet uses RNG
        if(Utils::isRNGRequired(m_ToeplitzConnectivityInitialiser.getDiagonalBuildCodeTokens())) {
            throw std::runtime_error("TOEPLITZ connectivity cannot currently access RNG.");
        }

        // If the weight update model has code for postsynaptic-spike triggered updating, give an error
        if(!Utils::areTokensEmpty(m_WUPostLearnCodeTokens)) {
            throw std::runtime_error("TOEPLITZ connectivity cannot be used for synapse groups with postsynaptic spike-triggered learning");
        }

        // If toeplitz initialisation snippet provides a function to calculate kernel size, call it
        auto calcKernelSizeFunc = m_ToeplitzConnectivityInitialiser.getSnippet()->getCalcKernelSizeFunc();
        if(calcKernelSizeFunc) {
            m_KernelSize = calcKernelSizeFunc(m_ToeplitzConnectivityInitialiser.getParams());
        }
        else {
            throw std::runtime_error("TOEPLITZ connectivity requires a toeplitz connectivity initialisation snippet which specifies a kernel size.");
        }

        // If toeplitz initialisation snippet provides a function to calculate max row length, call it
        auto calcMaxRowLengthFunc = m_ToeplitzConnectivityInitialiser.getSnippet()->getCalcMaxRowLengthFunc();
        if(calcMaxRowLengthFunc) {
            m_MaxConnections = calcMaxRowLengthFunc(srcNeuronGroup->getNumNeurons(), trgNeuronGroup->getNumNeurons(),
                                                    m_ToeplitzConnectivityInitialiser.getParams());
        }
        else {
            throw std::runtime_error("TOEPLITZ connectivity requires a toeplitz connectivity initialisation snippet which specifies a max row length.");
        }

        // No postsynaptic update through toeplitz matrices for now
        m_MaxSourceConnections = 0;
    }
    // Otherwise
    else {
        // If sparse connectivitity initialisation snippet provides a function to calculate kernel size, call it
        auto calcKernelSizeFunc = m_SparseConnectivityInitialiser.getSnippet()->getCalcKernelSizeFunc();
        if(calcKernelSizeFunc) {
            m_KernelSize = calcKernelSizeFunc(m_SparseConnectivityInitialiser.getParams());
        }

        // If connectivitity initialisation snippet provides a function to calculate row length, call it
        // **NOTE** only do this for sparse connectivity as this should not be set for bitmasks
        auto calcMaxRowLengthFunc = m_SparseConnectivityInitialiser.getSnippet()->getCalcMaxRowLengthFunc();
        if(calcMaxRowLengthFunc && (m_MatrixType & SynapseMatrixConnectivity::SPARSE)) {
            m_MaxConnections = calcMaxRowLengthFunc(srcNeuronGroup->getNumNeurons(), trgNeuronGroup->getNumNeurons(),
                                                    m_SparseConnectivityInitialiser.getParams());
        }
        // Otherwise, default to the size of the target population
        else {
            m_MaxConnections = trgNeuronGroup->getNumNeurons();
        }

        // If connectivitity initialisation snippet provides a function to calculate row length, call it
        // **NOTE** only do this for sparse connectivity as this should not be set for bitmasks
        auto calcMaxColLengthFunc = m_SparseConnectivityInitialiser.getSnippet()->getCalcMaxColLengthFunc();
        if(calcMaxColLengthFunc && (m_MatrixType & SynapseMatrixConnectivity::SPARSE)) {
            m_MaxSourceConnections = calcMaxColLengthFunc(srcNeuronGroup->getNumNeurons(), trgNeuronGroup->getNumNeurons(),
                                                          m_SparseConnectivityInitialiser.getParams());
        }
        // Otherwise, default to the size of the source population
        else {
            m_MaxSourceConnections = srcNeuronGroup->getNumNeurons();
        }
    }

    // If connectivity initialisation snippet defines a kernel and matrix type doesn't support it, give error
    if(!m_KernelSize.empty() && (m_MatrixType != SynapseMatrixType::PROCEDURAL) && (m_MatrixType != SynapseMatrixType::TOEPLITZ)
       && (m_MatrixType != SynapseMatrixType::SPARSE) && (m_MatrixType != SynapseMatrixType::PROCEDURAL_KERNELG)) 
    {
        throw std::runtime_error("BITMASK connectivity can only be used with weight update models without variables like StaticPulseConstantWeight.");
    }

    // If connectivity is dense and there is connectivity initialiser code, give error
    if((m_MatrixType & SynapseMatrixConnectivity::DENSE) 
       && (!Utils::areTokensEmpty(m_SparseConnectivityInitialiser.getRowBuildCodeTokens()) 
           || !Utils::areTokensEmpty(m_SparseConnectivityInitialiser.getColBuildCodeTokens()))) 
    {
        throw std::runtime_error("Cannot use DENSE connectivity with connectivity initialisation snippet.");
    }

    // If synapse group uses sparse or procedural connectivity but no kernel size is provided, 
    // check that no variable's initialisation snippets require a kernel
    if(((m_MatrixType == SynapseMatrixType::SPARSE) || (m_MatrixType == SynapseMatrixType::PROCEDURAL)) &&
       m_KernelSize.empty() && std::any_of(getWUVarInitialisers().cbegin(), getWUVarInitialisers().cend(), 
                                           [](const auto &v) { return v.second.isKernelRequired(); }))
    {
        throw std::runtime_error("Variable initialisation snippets which use $(id_kernel) must be used with a connectivity initialisation snippet which specifies how kernel size is calculated.");
    }

    // Check that the source neuron group supports the desired number of delay steps
    srcNeuronGroup->checkNumDelaySlots(delaySteps);
}
//----------------------------------------------------------------------------
void SynapseGroup::finalise(double dt)
{
    auto wuDerivedParams = getWUModel()->getDerivedParams();
    auto psDerivedParams = getPSModel()->getDerivedParams();

    // Loop through WU derived parameters
    for(const auto &d : wuDerivedParams) {
        m_WUDerivedParams.emplace(d.name, d.func(m_WUParams, dt));
    }

    // Loop through PSM derived parameters
    for(const auto &d : psDerivedParams) {
        m_PSDerivedParams.emplace(d.name, d.func(m_PSParams, dt));
    }

    // Initialise derived parameters for WU variable initialisers
    for(auto &v : m_WUVarInitialisers) {
        v.second.finalise(dt);
    }

    // Initialise derived parameters for PSM variable initialisers
    for(auto &v : m_PSVarInitialisers) {
        v.second.finalise(dt);
    }

    // Initialise derived parameters for WU presynaptic variable initialisers
    for(auto &v : m_WUPreVarInitialisers) {
        v.second.finalise(dt);
    }
    
    // Initialise derived parameters for WU postsynaptic variable initialisers
    for(auto &v : m_WUPostVarInitialisers) {
        v.second.finalise(dt);
    }

    // Initialise any derived connectivity initialiser parameters
    m_SparseConnectivityInitialiser.finalise(dt);
    m_ToeplitzConnectivityInitialiser.finalise(dt);

    // Mark any pre or postsyaptic neuron variables referenced in sim code as requiring queues
    if (!Utils::areTokensEmpty(m_WUSimCodeTokens)) {
        getSrcNeuronGroup()->updatePreVarQueues(m_WUSimCodeTokens);
        getTrgNeuronGroup()->updatePostVarQueues(m_WUSimCodeTokens);
    }

    // Mark any pre or postsyaptic neuron variables referenced in event code as requiring queues
    if (!Utils::areTokensEmpty(m_WUEventCodeTokens)) {
        getSrcNeuronGroup()->updatePreVarQueues(m_WUEventCodeTokens);
        getTrgNeuronGroup()->updatePostVarQueues(m_WUEventCodeTokens);
    }

    // Mark any pre or postsyaptic neuron variables referenced in postsynaptic update code as requiring queues
    if (!Utils::areTokensEmpty(m_WUPostLearnCodeTokens)) {
        getSrcNeuronGroup()->updatePreVarQueues(m_WUPostLearnCodeTokens);
        getTrgNeuronGroup()->updatePostVarQueues(m_WUPostLearnCodeTokens);
    }

    // Mark any pre or postsyaptic neuron variables referenced in synapse dynamics code as requiring queues
    if (!Utils::areTokensEmpty(m_WUSynapseDynamicsCodeTokens)) {
        getSrcNeuronGroup()->updatePreVarQueues(m_WUSynapseDynamicsCodeTokens);
        getTrgNeuronGroup()->updatePostVarQueues(m_WUSynapseDynamicsCodeTokens);
    }
}
//----------------------------------------------------------------------------
bool SynapseGroup::canPSBeFused() const
{
    // If any postsynaptic model variables aren't initialised to constant values, this synapse group's postsynaptic model can't be merged
    // **NOTE** hash check will compare these constant values
    if(std::any_of(getPSVarInitialisers().cbegin(), getPSVarInitialisers().cend(), 
                   [](const auto &v){ return (dynamic_cast<const InitVarSnippet::Constant*>(v.second.getSnippet()) == nullptr); }))
    {
        return false;
    }
    
    // Loop through EGPs
    // **NOTE** this is kind of silly as, if it's not referenced in either of 
    // these code strings, there wouldn't be a lot of point in a PSM EGP existing!
    for(const auto &egp : getPSModel()->getExtraGlobalParams()) {
        // If this EGP is referenced in decay code, return false
        if(Utils::isIdentifierReferenced(egp.name, getPSDecayCodeTokens())) {
            return false;
        }
        
        // If this EGP is referenced in apply input code, return false
        if(Utils::isIdentifierReferenced(egp.name, getPSApplyInputCodeTokens())) {
            return false;
        }
    }
    
    return true;
}
//----------------------------------------------------------------------------
bool SynapseGroup::canWUMPreUpdateBeFused() const
{
    // If any presynaptic variables aren't initialised to constant values, this synapse group's presynaptic update can't be merged
    // **NOTE** hash check will compare these constant values
    if(std::any_of(getWUPreVarInitialisers().cbegin(), getWUPreVarInitialisers().cend(), 
                   [](const auto &v){ return (dynamic_cast<const InitVarSnippet::Constant*>(v.second.getSnippet()) == nullptr); }))
    {
        return false;
    }
    
    // Loop through EGPs
    for(const auto &egp : getWUModel()->getExtraGlobalParams()) {
        // If this EGP is referenced in presynaptic spike code, return false
        if(Utils::isIdentifierReferenced(egp.name, getWUPreSpikeCodeTokens())) {
            return false;
        }
        
        // If this EGP is referenced in presynaptic dynamics code, return false
        if(Utils::isIdentifierReferenced(egp.name, getWUPreDynamicsCodeTokens())) {
            return false;
        }
    }
    return true;
}
//----------------------------------------------------------------------------
bool SynapseGroup::canWUMPostUpdateBeFused() const
{
    // If any postsynaptic variables aren't initialised to constant values, this synapse group's postsynaptic update can't be merged
    // **NOTE** hash check will compare these constant values
    if(std::any_of(getWUPostVarInitialisers().cbegin(), getWUPostVarInitialisers().cend(), 
                   [](const auto &v){ return (dynamic_cast<const InitVarSnippet::Constant*>(v.second.getSnippet()) == nullptr); }))
    {
        return false;
    }
    
    // Loop through EGPs
    for(const auto &egp : getWUModel()->getExtraGlobalParams()) {
        // If this EGP is referenced in postsynaptic spike code, return false
        if(Utils::isIdentifierReferenced(egp.name, getWUPostSpikeCodeTokens())) {
            return false;
        }
        
        // If this EGP is referenced in postsynaptic dynamics code, return false
        if(Utils::isIdentifierReferenced(egp.name, getWUPostDynamicsCodeTokens())) {
            return false;
        }
    }
    return true;
}
//----------------------------------------------------------------------------
bool SynapseGroup::isDendriticDelayRequired() const
{
    // If addToPostDelay function is used in sim code, return true
    if(Utils::isIdentifierReferenced("addToPostDelay", getWUSimCodeTokens())) {
        return true;
    }

    // If addToPostDelay function is used in event code, return true
    if(Utils::isIdentifierReferenced("addToPostDelay", getWUEventCodeTokens())) {
        return true;
    }

    // If addToPostDelay function is used in synapse dynamics, return tru
    if(Utils::isIdentifierReferenced("addToPostDelay", getWUSynapseDynamicsCodeTokens())) {
        return true;
    }

    return false;
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPresynapticOutputRequired() const
{
    // If addToPre function is used in sim code, return true
    if(Utils::isIdentifierReferenced("addToPre", getWUSimCodeTokens())) {
        return true;
    }

    // If addToPre function is used in event code, return true
    if(Utils::isIdentifierReferenced("addToPre", getWUEventCodeTokens())) {
        return true;
    }

    // If addToPre function is used in learn post code, return true
    if(Utils::isIdentifierReferenced("addToPre", getWUPostLearnCodeTokens())) {
        return true;
    }

    // If addToPre function is used in synapse dynamics, return tru
    if(Utils::isIdentifierReferenced("addToPre", getWUSynapseDynamicsCodeTokens())) {
        return true;
    }

    return false;
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPostsynapticOutputRequired() const
{
    if(isDendriticDelayRequired()) {
        return true;
    }
    else {
        // If addToPost function is used in sim code, return true
        if(Utils::isIdentifierReferenced("addToPost", getWUSimCodeTokens())) {
            return true;
        }

        // If addToPost function is used in event code, return true
        if(Utils::isIdentifierReferenced("addToPost", getWUEventCodeTokens())) {
            return true;
        }

        // If addToPost function is used in synapse dynamics, return tru
        if(Utils::isIdentifierReferenced("addToPost", getWUSynapseDynamicsCodeTokens())) {
            return true;
        }

        return false;
    }
}
//----------------------------------------------------------------------------
bool SynapseGroup::isProceduralConnectivityRNGRequired() const
{
    if(m_MatrixType & SynapseMatrixConnectivity::PROCEDURAL) {
        return m_SparseConnectivityInitialiser.isRNGRequired();
    }
    else if(m_MatrixType & SynapseMatrixConnectivity::TOEPLITZ) {
        return m_ToeplitzConnectivityInitialiser.isRNGRequired();
    }
    else {
        return false;
    }
}
//----------------------------------------------------------------------------
bool SynapseGroup::isWUInitRNGRequired() const
{
    // If initialising the weight update variables require an RNG, return true
    if(Utils::isRNGRequired(m_WUVarInitialisers)) {
        return true;
    }

    // Return true if matrix has sparse or bitmask connectivity and an RNG is required to initialise connectivity
    return (((m_MatrixType & SynapseMatrixConnectivity::SPARSE) || (m_MatrixType & SynapseMatrixConnectivity::BITMASK))
            && m_SparseConnectivityInitialiser.isRNGRequired());
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPSVarInitRequired() const
{
    return std::any_of(m_PSVarInitialisers.cbegin(), m_PSVarInitialisers.cend(),
                       [](const auto &init)
                       { 
                           return !Utils::areTokensEmpty(init.second.getCodeTokens());
                       });
}
//----------------------------------------------------------------------------
bool SynapseGroup::isWUVarInitRequired() const
{
    // If this synapse group has per-synapse or kernel state variables, 
    // return true if any of them have initialisation code which doesn't require a kernel
    if ((getMatrixType() & SynapseMatrixWeight::INDIVIDUAL) || (getMatrixType() & SynapseMatrixWeight::KERNEL)) {
        return std::any_of(m_WUVarInitialisers.cbegin(), m_WUVarInitialisers.cend(),
                           [](const auto &init)
                           { 
                               return !Utils::areTokensEmpty(init.second.getCodeTokens()) && !init.second.isKernelRequired();
                           });
    }
    else {
        return false;
    }
}
//----------------------------------------------------------------------------
bool SynapseGroup::isWUPreVarInitRequired() const
{
    return std::any_of(m_WUPreVarInitialisers.cbegin(), m_WUPreVarInitialisers.cend(),
                       [](const auto &init)
                       { 
                           return !Utils::areTokensEmpty(init.second.getCodeTokens());
                       });
}
//----------------------------------------------------------------------------
bool SynapseGroup::isWUPostVarInitRequired() const
{
    return std::any_of(m_WUPostVarInitialisers.cbegin(), m_WUPostVarInitialisers.cend(),
                       [](const auto &init)
                       { 
                           return !Utils::areTokensEmpty(init.second.getCodeTokens());
                       });
}
//----------------------------------------------------------------------------
bool SynapseGroup::isSparseConnectivityInitRequired() const
{
    // Return true if the matrix type is sparse or bitmask 
    // and there is code to initialise sparse connectivity 
    return (((m_MatrixType & SynapseMatrixConnectivity::SPARSE) || (m_MatrixType & SynapseMatrixConnectivity::BITMASK))
            && (!Utils::areTokensEmpty(getConnectivityInitialiser().getRowBuildCodeTokens()) 
                || !Utils::areTokensEmpty(getConnectivityInitialiser().getColBuildCodeTokens())));
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPreTimeReferenced(const std::string &identifier) const
{
    return (Utils::isIdentifierReferenced(identifier, getWUEventCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUEventThresholdCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUPostLearnCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUPreDynamicsCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUPreSpikeCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUSimCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUSynapseDynamicsCodeTokens()));
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPostTimeReferenced(const std::string &identifier) const
{
    return (Utils::isIdentifierReferenced(identifier, getWUEventCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUEventThresholdCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUPostLearnCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUPostDynamicsCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUPostSpikeCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUSimCodeTokens())
            || Utils::isIdentifierReferenced(identifier, getWUSynapseDynamicsCodeTokens()));
}
//----------------------------------------------------------------------------
bool SynapseGroup::canPreOutputBeFused() const
{
    // There are no variables or other non-constant objects, so these can presumably always be fused
    return true;
}
//----------------------------------------------------------------------------
const Type::ResolvedType &SynapseGroup::getSparseIndType() const
{
    // If narrow sparse inds are enabled
    if(m_NarrowSparseIndEnabled) {
        // If number of target neurons can be represented using a uint8, use this type
        const unsigned int numTrgNeurons = getTrgNeuronGroup()->getNumNeurons();
        if(numTrgNeurons <= std::numeric_limits<uint8_t>::max()) {
            return Type::Uint8;
        }
        // Otherwise, if they can be represented as a uint16, use this type
        else if(numTrgNeurons <= std::numeric_limits<uint16_t>::max()) {
            return Type::Uint16;
        }
    }

    // Otherwise, use 32-bit int
    return Type::Uint32;
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getWUModel()->getHashDigest(), hash);
    Utils::updateHash(getDelaySteps(), hash);
    Utils::updateHash(getBackPropDelaySteps(), hash);
    Utils::updateHash(getMaxDendriticDelayTimesteps(), hash);
    Type::updateHash(getSparseIndType(), hash);
    Utils::updateHash(getNumThreadsPerSpike(), hash);
    Utils::updateHash(isEventThresholdReTestRequired(), hash);
    Utils::updateHash(getSpanType(), hash);
    Utils::updateHash(isPSModelFused(), hash);
    Utils::updateHash(getSrcNeuronGroup()->getNumDelaySlots(), hash);
    Utils::updateHash(getTrgNeuronGroup()->getNumDelaySlots(), hash);
    Utils::updateHash(getMatrixType(), hash);

    // If weights are procedural, include variable initialiser hashes
    if(getMatrixType() & SynapseMatrixWeight::PROCEDURAL) {
        for(const auto &w : getWUVarInitialisers()) {
            Utils::updateHash(w.first, hash);
            Utils::updateHash(w.second.getHashDigest(), hash);
        }
    }

    // If connectivity is procedural, include connectivitiy initialiser hash
    if(getMatrixType() & SynapseMatrixConnectivity::PROCEDURAL) {
        Utils::updateHash(getConnectivityInitialiser().getHashDigest(), hash);
    }

    // If connectivity is Toepltiz, include Toeplitz connectivitiy initialiser hash
    if(getMatrixType() & SynapseMatrixConnectivity::TOEPLITZ) {
        Utils::updateHash(getToeplitzConnectivityInitialiser().getHashDigest(), hash);
    }
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUPreHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getWUModel()->getHashDigest(), hash);
    Utils::updateHash((getDelaySteps() != 0), hash);
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUPostHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getWUModel()->getHashDigest(), hash);
    Utils::updateHash((getBackPropDelaySteps() != 0), hash);
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getPSHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getPSModel()->getHashDigest(), hash);
    Utils::updateHash(getMaxDendriticDelayTimesteps(), hash);
    Utils::updateHash(getPSTargetVar(), hash);

    // Loop through neuron variable references and update hash with 
    // name of target variable. These must be the same across merged group
    // as these variable references are just implemented as aliases for neuron variables
    for(const auto &v : getPSNeuronVarReferences()) {
        Utils::updateHash(v.second.getVarName(), hash);
    };

    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getPSFuseHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getPSModel()->getHashDigest(), hash);
    Utils::updateHash(getMaxDendriticDelayTimesteps(), hash);
    Utils::updateHash(getPSTargetVar(), hash);
    Utils::updateHash(getPSParams(), hash);
    Utils::updateHash(getPSDerivedParams(), hash);
    
    // Loop through PSM variable initialisers and hash first parameter.
    // Due to SynapseGroup::canPSBeFused, all initialiser snippets
    // will be constant and have a single parameter containing the value
    for(const auto &w : getPSVarInitialisers()) {
        assert(w.second.getParams().size() == 1);
        Utils::updateHash(w.second.getParams().at("constant"), hash);
    }

    // Loop through neuron variable references and update hash with 
    // name of target variable. These must be the same across merged group
    // as these variable references are just implemented as aliases for neuron variables
    for(const auto &v : getPSNeuronVarReferences()) {
        Utils::updateHash(v.second.getVarName(), hash);
    };
    
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getPreOutputHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getPreTargetVar(), hash);
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUPreFuseHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getWUModel()->getPreHashDigest(), hash);
    Utils::updateHash(getDelaySteps(), hash);

    // Loop through presynaptic variable initialisers and hash first parameter.
    // Due to SynapseGroup::canWUMPreUpdateBeFused, all initialiser snippets
    // will be constant and have a single parameter containing the value
    for(const auto &w : getWUPreVarInitialisers()) {
        assert(w.second.getParams().size() == 1);
        Utils::updateHash(w.second.getParams().at("constant"), hash);
    }

    // Loop through weight update model parameters and, if they are referenced
    // in presynaptic spike or dynamics code, include their value in hash
    for(const auto &p : getWUModel()->getParamNames()) {
        if(Utils::isIdentifierReferenced(p, getWUPreSpikeCodeTokens())
           || Utils::isIdentifierReferenced(p, getWUPreDynamicsCodeTokens())) 
        {
            Utils::updateHash(getWUParams().at(p), hash);
        }
    }

    // Loop through weight update model parameters and, if they are referenced
    // in presynaptic spike or dynamics code, include their value in hash
    for(const auto &d : getWUModel()->getDerivedParams()) {
        if(Utils::isIdentifierReferenced(d.name, getWUPreSpikeCodeTokens())
           || Utils::isIdentifierReferenced(d.name, getWUPreDynamicsCodeTokens()))
        {
            Utils::updateHash(getWUDerivedParams().at(d.name), hash);
        }
    }

    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUPostFuseHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getWUModel()->getPostHashDigest(), hash);
    Utils::updateHash(getBackPropDelaySteps(), hash);

    // Loop through postsynaptic variable initialisers and hash first parameter.
    // Due to SynapseGroup::canWUMPostUpdateBeFused, all initialiser snippets
    // will be constant and have a single parameter containing the value
    for(const auto &w : getWUPostVarInitialisers()) {
        assert(w.second.getParams().size() == 1);
        Utils::updateHash(w.second.getParams().at("constant"), hash);
    }

    // Loop through weight update model parameters and, if they are referenced
    // in presynaptic spike or dynamics code, include their value in hash
    for(const auto &p : getWUModel()->getParamNames()) {
       if(Utils::isIdentifierReferenced(p, getWUPostSpikeCodeTokens())
           || Utils::isIdentifierReferenced(p, getWUPostDynamicsCodeTokens())) 
        {
            Utils::updateHash(getWUParams().at(p), hash);
        }
    }

    // Loop through weight update model parameters and, if they are referenced
    // in presynaptic spike or dynamics code, include their value in hash
    for(const auto &d : getWUModel()->getDerivedParams()) {
        if(Utils::isIdentifierReferenced(d.name, getWUPostSpikeCodeTokens())
           || Utils::isIdentifierReferenced(d.name, getWUPostDynamicsCodeTokens())) 
        {
            Utils::updateHash(getWUDerivedParams().at(d.name), hash);
        }
    }

    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getDendriticDelayUpdateHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getMaxDendriticDelayTimesteps(), hash);
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUInitHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getMatrixType(), hash);
    Type::updateHash(getSparseIndType(), hash);
    Utils::updateHash(getWUModel()->getVars(), hash);

    Utils::updateHash(Utils::areTokensEmpty(getWUSynapseDynamicsCodeTokens()), hash);
    Utils::updateHash(Utils::areTokensEmpty(getWUPostLearnCodeTokens()), hash);

    // Include variable initialiser hashes
    for(const auto &w : getWUVarInitialisers()) {
        Utils::updateHash(w.first, hash);
        Utils::updateHash(w.second.getHashDigest(), hash);
    }
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUPreInitHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getWUModel()->getPreVars(), hash);

    // Include presynaptic variable initialiser hashes
    for(const auto &w : getWUPreVarInitialisers()) {
        Utils::updateHash(w.first, hash);
        Utils::updateHash(w.second.getHashDigest(), hash);
    }
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUPostInitHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getWUModel()->getPostVars(), hash);

    // Include postsynaptic variable initialiser hashes
    for(const auto &w : getWUPostVarInitialisers()) {
        Utils::updateHash(w.first, hash);
        Utils::updateHash(w.second.getHashDigest(), hash);
    }
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getPSInitHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getMaxDendriticDelayTimesteps(), hash);
    Utils::updateHash(getPSModel()->getVars(), hash);

    // Include postsynaptic model variable initialiser hashes
    for(const auto &p : getPSVarInitialisers()) {
        Utils::updateHash(p.first, hash);
        Utils::updateHash(p.second.getHashDigest(), hash);
    }
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getPreOutputInitHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getConnectivityInitHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getConnectivityInitialiser().getHashDigest(), hash);
    Utils::updateHash(getMatrixType(), hash);
    Type::updateHash(getSparseIndType(), hash);
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getConnectivityHostInitHashDigest() const
{
    return getConnectivityInitialiser().getHashDigest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getVarLocationHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getInSynLocation(), hash);
    Utils::updateHash(getDendriticDelayLocation(), hash);
    Utils::updateHash(getSparseConnectivityLocation(), hash);
    Utils::updateHash(m_WUVarLocation, hash);
    Utils::updateHash(m_WUPreVarLocation, hash);
    Utils::updateHash(m_WUPostVarLocation, hash);
    Utils::updateHash(m_PSVarLocation, hash);
    Utils::updateHash(m_WUExtraGlobalParamLocation, hash);
    Utils::updateHash(m_PSExtraGlobalParamLocation, hash);
    return hash.get_digest();
}
}   // namespace GeNN
