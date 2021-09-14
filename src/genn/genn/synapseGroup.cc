#include "synapseGroup.h"

// Standard includes
#include <algorithm>
#include <cmath>
#include <iostream>

// GeNN includes
#include "gennUtils.h"
#include "neuronGroupInternal.h"
#include "synapseGroupInternal.h"

//----------------------------------------------------------------------------
// Anonymous namespace
//----------------------------------------------------------------------------
namespace
{
std::vector<double> getConstInitVals(const std::vector<Models::VarInit> &varInitialisers)
{
    // Reserve initial values to match initialisers
    std::vector<double> initVals;
    initVals.reserve(varInitialisers.size());

    // Transform variable initialisers into a vector of doubles
    std::transform(varInitialisers.cbegin(), varInitialisers.cend(), std::back_inserter(initVals),
                   [](const Models::VarInit &v)
                   {
                       // Check
                       if(dynamic_cast<const InitVarSnippet::Constant*>(v.getSnippet()) == nullptr) {
                           throw std::runtime_error("Only 'Constant' variable initialisation snippets can be used to initialise state variables of synapse groups using GLOBALG");
                       }

                       // Return the first parameter (the value)
                       return v.getParams()[0];
                   });

    return initVals;
}
}   // Anonymous namespace

// ------------------------------------------------------------------------
// SynapseGroup
// ------------------------------------------------------------------------
void SynapseGroup::setWUVarLocation(const std::string &varName, VarLocation loc)
{
    if(isWeightSharingSlave()) {
        throw std::runtime_error("setWUVarLocation: Synapse group is a weight sharing slave. Weight update var location can only be set on the master.");
    }
    else {
        m_WUVarLocation[getWUModel()->getVarIndex(varName)] = loc;
    }
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
    const size_t extraGlobalParamIndex = getWUModel()->getExtraGlobalParamIndex(paramName);
    if(!Utils::isTypePointer(getWUModel()->getExtraGlobalParams()[extraGlobalParamIndex].type)) {
        throw std::runtime_error("Only extra global parameters with a pointer type have a location");
    }
    m_WUExtraGlobalParamLocation[extraGlobalParamIndex] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setPSVarLocation(const std::string &varName, VarLocation loc)
{
    m_PSVarLocation[getPSModel()->getVarIndex(varName)] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setPSExtraGlobalParamLocation(const std::string &paramName, VarLocation loc)
{
    const size_t extraGlobalParamIndex = getPSModel()->getExtraGlobalParamIndex(paramName);
    if(!Utils::isTypePointer(getPSModel()->getExtraGlobalParams()[extraGlobalParamIndex].type)) {
        throw std::runtime_error("Only extra global parameters with a pointer type have a location");
    }
    m_PSExtraGlobalParamLocation[extraGlobalParamIndex] = loc;
}
//----------------------------------------------------------------------------
void SynapseGroup::setSparseConnectivityExtraGlobalParamLocation(const std::string &paramName, VarLocation loc)
{
    if(isWeightSharingSlave()) {
        throw std::runtime_error("setSparseConnectivityExtraGlobalParamLocation: Synapse group is a weight sharing slave. Sparse connectivity EGP location can only be set on the master.");
    }
    else {
        const size_t extraGlobalParamIndex = m_ConnectivityInitialiser.getSnippet()->getExtraGlobalParamIndex(paramName);
        if(!Utils::isTypePointer(m_ConnectivityInitialiser.getSnippet()->getExtraGlobalParams()[extraGlobalParamIndex].type)) {
            throw std::runtime_error("Only extra global parameters with a pointer type have a location");
        }
        m_ConnectivityExtraGlobalParamLocation[extraGlobalParamIndex] = loc;
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setSparseConnectivityLocation(VarLocation loc)
{ 
    if(isWeightSharingSlave()) {
        throw std::runtime_error("setSparseConnectivityLocation: Synapse group is a weight sharing slave. Sparse connectivity location can only be set on the master.");
    }
    else {
        m_SparseConnectivityLocation = loc;
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setMaxConnections(unsigned int maxConnections)
{
    if(isWeightSharingSlave()) {
        throw std::runtime_error("setMaxConnections: Synapse group is a weight sharing slave. Max connections can only be set on the master.");
    }
    else {
        if(getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
            if(m_ConnectivityInitialiser.getSnippet()->getCalcMaxRowLengthFunc()) {
                throw std::runtime_error("setMaxConnections: Synapse group already has max connections defined by connectivity initialisation snippet.");
            }

            m_MaxConnections = maxConnections;
        }
        else {
            throw std::runtime_error("setMaxConnections: Synapse group is densely connected. Setting max connections is not required in this case.");
        }
    }
}
//----------------------------------------------------------------------------
void SynapseGroup::setMaxSourceConnections(unsigned int maxConnections)
{
    if(isWeightSharingSlave()) {
        throw std::runtime_error("setMaxSourceConnections: Synapse group is a weight sharing slave. Max source connections can only be set on the master.");
    }
    else {
        if(getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
            if(m_ConnectivityInitialiser.getSnippet()->getCalcMaxColLengthFunc()) {
                throw std::runtime_error("setMaxSourceConnections: Synapse group already has max source connections defined by connectivity initialisation snippet.");
            }

            m_MaxSourceConnections = maxConnections;
        }
        else {
            throw std::runtime_error("setMaxSourceConnections: Synapse group is densely connected. Setting max connections is not required in this case.");
        }
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
    if(isWeightSharingSlave()) {
        throw std::runtime_error("setNarrowSparseIndEnabled: Synapse group is a weight sharing slave. Sparse index type can only be set on the master.");
    }
    else {
        if(getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
            m_NarrowSparseIndEnabled = enabled;
        }
        else {
            throw std::runtime_error("setNarrowSparseIndEnabled: This function can only be used on synapse groups with sparse connectivity.");
        }
    }
}
//----------------------------------------------------------------------------
unsigned int SynapseGroup::getMaxConnections() const
{ 
    // **NOTE** these get retrived from weight sharing master 
    // as they can be set AFTER creation of synapse group
    return isWeightSharingSlave() ? getWeightSharingMaster()->getMaxConnections() : m_MaxConnections; 
}
//----------------------------------------------------------------------------
unsigned int SynapseGroup::getMaxSourceConnections() const
{ 
    // **NOTE** these get retrived from weight sharing master 
    // as they can be set AFTER creation of synapse group
    return isWeightSharingSlave() ? getWeightSharingMaster()->getMaxSourceConnections() : m_MaxSourceConnections;
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getSparseConnectivityLocation() const
{ 
    return isWeightSharingSlave() ? getWeightSharingMaster()->getSparseConnectivityLocation() : m_SparseConnectivityLocation;
}
//----------------------------------------------------------------------------
bool SynapseGroup::isTrueSpikeRequired() const
{
    return !getWUModel()->getSimCode().empty();
}
//----------------------------------------------------------------------------
bool SynapseGroup::isSpikeEventRequired() const
{
     return !getWUModel()->getEventCode().empty();
}
//----------------------------------------------------------------------------
const std::vector<double> SynapseGroup::getWUConstInitVals() const
{
    return getConstInitVals(m_WUVarInitialisers);
}
//----------------------------------------------------------------------------
const std::vector<double> SynapseGroup::getPSConstInitVals() const
{
    return getConstInitVals(m_PSVarInitialisers);
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
    // **NOTE** these get retrived from weight sharing master 
    // as they can be set AFTER creation of synapse group
    if(isWeightSharingSlave()) {
        return getWeightSharingMaster()->getWUVarLocation(var);
    }
    else {
        return m_WUVarLocation[getWUModel()->getVarIndex(var)];
    }
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getWUVarLocation(size_t index) const
{ 
    // **NOTE** these get retrived from weight sharing master 
    // as they can be set AFTER creation of synapse group
    if(isWeightSharingSlave()) {
        return getWeightSharingMaster()->getWUVarLocation(index);
    }
    else {
        return m_WUVarLocation.at(index);
    }
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
    // **NOTE** these get retrived from weight sharing master 
    // as they can be set AFTER creation of synapse group
    if(isWeightSharingSlave()) {
        return getWeightSharingMaster()->getSparseConnectivityExtraGlobalParamLocation(paramName);
    }
    else {
        return m_ConnectivityExtraGlobalParamLocation[m_ConnectivityInitialiser.getSnippet()->getExtraGlobalParamIndex(paramName)];
    }
}
//----------------------------------------------------------------------------
VarLocation SynapseGroup::getSparseConnectivityExtraGlobalParamLocation(size_t index) const
{ 
    // **NOTE** these get retrived from weight sharing master 
    // as they can be set AFTER creation of synapse group
    if(isWeightSharingSlave()) {
        return getWeightSharingMaster()->getSparseConnectivityExtraGlobalParamLocation(index);
    }
    else {
        return m_ConnectivityExtraGlobalParamLocation.at(index);
    }
}
//----------------------------------------------------------------------------
bool SynapseGroup::isDendriticDelayRequired() const
{
    // If addToInSynDelay function is used in sim code, return true
    if(getWUModel()->getSimCode().find("$(addToInSynDelay") != std::string::npos) {
        return true;
    }

    // If addToInSynDelay function is used in synapse dynamics, return true
    if(getWUModel()->getSynapseDynamicsCode().find("$(addToInSynDelay") != std::string::npos) {
        return true;
    }

    return false;
}
//----------------------------------------------------------------------------
bool SynapseGroup::isProceduralConnectivityRNGRequired() const
{
    return ((m_MatrixType & SynapseMatrixConnectivity::PROCEDURAL) &&
            Utils::isRNGRequired(m_ConnectivityInitialiser.getSnippet()->getRowBuildCode()));
}
//----------------------------------------------------------------------------
bool SynapseGroup::isPSInitRNGRequired() const
{
    // If initialising the postsynaptic variables require an RNG, return true
    return Utils::isRNGRequired(m_PSVarInitialisers);
}
//----------------------------------------------------------------------------
bool SynapseGroup::isWUInitRNGRequired() const
{
    // If initialising the weight update variables require an RNG, return true
    if(Utils::isRNGRequired(m_WUVarInitialisers)) {
        return true;
    }

    // Return true if matrix has sparse or bitmask connectivity and an RNG is required to initialise connectivity
    const auto *snippet = m_ConnectivityInitialiser.getSnippet();
    return (((m_MatrixType & SynapseMatrixConnectivity::SPARSE) || (m_MatrixType & SynapseMatrixConnectivity::BITMASK))
            && (Utils::isRNGRequired(snippet->getRowBuildCode()) || Utils::isRNGRequired(snippet->getColBuildCode())));
}
//----------------------------------------------------------------------------
bool SynapseGroup::isWUPreInitRNGRequired() const
{
    return Utils::isRNGRequired(m_WUPreVarInitialisers);
}
//----------------------------------------------------------------------------
bool SynapseGroup::isWUPostInitRNGRequired() const
{
    return Utils::isRNGRequired(m_WUPostVarInitialisers);
}
//----------------------------------------------------------------------------
bool SynapseGroup::isHostInitRNGRequired() const
{
    return (m_ConnectivityInitialiser.getSnippet()->getHostInitCode().find("$(rng)") != std::string::npos);
}
//----------------------------------------------------------------------------
bool SynapseGroup::isWUVarInitRequired() const
{
    // If this synapse group has per-synapse state variables and isn't a
    // weight sharing slave, return true if any of them have initialisation code which doesn't require a kernel
    if (!isWeightSharingSlave() && (getMatrixType() & SynapseMatrixWeight::INDIVIDUAL)) {
        return std::any_of(m_WUVarInitialisers.cbegin(), m_WUVarInitialisers.cend(),
                           [](const Models::VarInit &init)
                           { 
                               return !init.getSnippet()->getCode().empty() && !init.getSnippet()->requiresKernel(); 
                           });
    }
    else {
        return false;
    }
}
//----------------------------------------------------------------------------
bool SynapseGroup::isSparseConnectivityInitRequired() const
{
    // Return true if the matrix type is sparse or bitmask, there is code to  
    // initialise sparse connectivity and synapse group isn't a weight sharing slave,
    const auto *snippet = getConnectivityInitialiser().getSnippet();
    return (((m_MatrixType & SynapseMatrixConnectivity::SPARSE) || (m_MatrixType & SynapseMatrixConnectivity::BITMASK))
            && (!snippet->getRowBuildCode().empty() || !snippet->getColBuildCode().empty())
            && !isWeightSharingSlave());
}
//----------------------------------------------------------------------------
SynapseGroup::SynapseGroup(const std::string &name, SynapseMatrixType matrixType, unsigned int delaySteps,
                           const WeightUpdateModels::Base *wu, const std::vector<double> &wuParams, const std::vector<Models::VarInit> &wuVarInitialisers, const std::vector<Models::VarInit> &wuPreVarInitialisers, const std::vector<Models::VarInit> &wuPostVarInitialisers,
                           const PostsynapticModels::Base *ps, const std::vector<double> &psParams, const std::vector<Models::VarInit> &psVarInitialisers,
                           NeuronGroupInternal *srcNeuronGroup, NeuronGroupInternal *trgNeuronGroup, const SynapseGroupInternal *weightSharingMaster,
                           const InitSparseConnectivitySnippet::Init &connectivityInitialiser,
                           VarLocation defaultVarLocation, VarLocation defaultExtraGlobalParamLocation,
                           VarLocation defaultSparseConnectivityLocation, bool defaultNarrowSparseIndEnabled)
    :   m_Name(name), m_SpanType(SpanType::POSTSYNAPTIC), m_NumThreadsPerSpike(1), m_DelaySteps(delaySteps), m_BackPropDelaySteps(0),
        m_MaxDendriticDelayTimesteps(1), m_MatrixType(matrixType),  m_SrcNeuronGroup(srcNeuronGroup), m_TrgNeuronGroup(trgNeuronGroup), m_WeightSharingMaster(weightSharingMaster),
        m_EventThresholdReTestRequired(false), m_NarrowSparseIndEnabled(defaultNarrowSparseIndEnabled),
        m_InSynLocation(defaultVarLocation),  m_DendriticDelayLocation(defaultVarLocation),
        m_WUModel(wu), m_WUParams(wuParams), m_WUVarInitialisers(wuVarInitialisers), m_WUPreVarInitialisers(wuPreVarInitialisers), m_WUPostVarInitialisers(wuPostVarInitialisers),
        m_PSModel(ps), m_PSParams(psParams), m_PSVarInitialisers(psVarInitialisers),
        m_WUVarLocation(wuVarInitialisers.size(), defaultVarLocation), m_WUPreVarLocation(wuPreVarInitialisers.size(), defaultVarLocation),
        m_WUPostVarLocation(wuPostVarInitialisers.size(), defaultVarLocation), m_WUExtraGlobalParamLocation(wu->getExtraGlobalParams().size(), defaultExtraGlobalParamLocation),
        m_PSVarLocation(psVarInitialisers.size(), defaultVarLocation), m_PSExtraGlobalParamLocation(ps->getExtraGlobalParams().size(), defaultExtraGlobalParamLocation),
        m_ConnectivityInitialiser(connectivityInitialiser), m_SparseConnectivityLocation(defaultSparseConnectivityLocation),
        m_ConnectivityExtraGlobalParamLocation(connectivityInitialiser.getSnippet()->getExtraGlobalParams().size(), defaultExtraGlobalParamLocation), m_PSModelTargetName(name)
{
    // Validate names
    Utils::validatePopName(name, "Synapse group");
    getWUModel()->validate();
    getPSModel()->validate();

    // If connectivity is procedural
    if(m_MatrixType & SynapseMatrixConnectivity::PROCEDURAL) {
        // If there's no row build code, give an error
        if(m_ConnectivityInitialiser.getSnippet()->getRowBuildCode().empty()) {
            throw std::runtime_error("Cannot use procedural connectivity without specifying a connectivity initialisation snippet with row building code");
        }

        // If there's column build code, give an error
        if(!m_ConnectivityInitialiser.getSnippet()->getColBuildCode().empty()) {
            throw std::runtime_error("Cannot use procedural connectivity with connectivity initialisation snippets with column building code");
        }

        // If the weight update model has code for postsynaptic-spike triggered updating, give an error
        if(!m_WUModel->getLearnPostCode().empty()) {
            throw std::runtime_error("Procedural connectivity cannot be used for synapse groups with postsynaptic spike-triggered learning");
        }

        // If weight update model has code for continuous synapse dynamics, give error
        // **THINK** this would actually be pretty trivial to implement
        if (!m_WUModel->getSynapseDynamicsCode().empty()) {
            throw std::runtime_error("Procedural connectivity cannot be used for synapse groups with continuous synapse dynamics");
        }
    }
    // Otherwise, if WEIGHTS are procedural e.g. in the case of DENSE_PROCEDURALG, give error if RNG is required for weights
    else if(m_MatrixType & SynapseMatrixWeight::PROCEDURAL) {
        if(::Utils::isRNGRequired(m_WUVarInitialisers)) {
            throw std::runtime_error("Procedural weights used without procedural connectivity cannot currently access RNG.");
        }
    }

    // If connectivitity initialisation snippet provides a function to calculate kernel size, call it
    auto calcKernelSizeFunc = m_ConnectivityInitialiser.getSnippet()->getCalcKernelSizeFunc();
    if(calcKernelSizeFunc) {
        m_KernelSize = calcKernelSizeFunc(m_ConnectivityInitialiser.getParams());
    }

    // If connectivity initialisation snippet defines a kernel and matrix type doesn't support it, give error
    if(!m_KernelSize.empty() && (m_MatrixType != SynapseMatrixType::PROCEDURAL_PROCEDURALG) 
       && (m_MatrixType != SynapseMatrixType::SPARSE_INDIVIDUALG)) 
    {
        throw std::runtime_error("Connectivity initialisation snippet which use a kernel can only be used with PROCEDURAL_PROCEDURALG or SPARSE_INDIVIDUALG connectivity.");
    }

    // If connectivity is dense and there is connectivity initialiser code, give error
    if((m_MatrixType & SynapseMatrixConnectivity::DENSE) 
       && (!m_ConnectivityInitialiser.getSnippet()->getRowBuildCode().empty() || !m_ConnectivityInitialiser.getSnippet()->getColBuildCode().empty())) 
    {
        throw std::runtime_error("Cannot use DENSE connectivity with connectivity initialisation snippet.");
    }

    // If synapse group uses sparse or procedural connectivity but no kernel size is provided, 
    // check that no variable's initialisation snippets require a kernel
    if(((m_MatrixType == SynapseMatrixType::SPARSE_INDIVIDUALG) || (m_MatrixType == SynapseMatrixType::PROCEDURAL_PROCEDURALG)) &&
       m_KernelSize.empty() &&  std::any_of(getWUVarInitialisers().cbegin(), getWUVarInitialisers().cend(), 
                                            [](const Models::VarInit &v) { return v.getSnippet()->requiresKernel(); }))
    {
        throw std::runtime_error("Variable initialisation snippets which use $(id_kernel) must be used with a connectivity initialisation snippet which specifies how kernel size is calculated.");
    }

    // If connectivitity initialisation snippet provides a function to calculate row length, call it
    // **NOTE** only do this for sparse connectivity as this should not be set for bitmasks
    auto calcMaxRowLengthFunc = m_ConnectivityInitialiser.getSnippet()->getCalcMaxRowLengthFunc();
    if(calcMaxRowLengthFunc && (m_MatrixType & SynapseMatrixConnectivity::SPARSE)) {
        m_MaxConnections = calcMaxRowLengthFunc(srcNeuronGroup->getNumNeurons(), trgNeuronGroup->getNumNeurons(),
                                                m_ConnectivityInitialiser.getParams());
    }
    // Otherwise, default to the size of the target population
    else {
        m_MaxConnections = trgNeuronGroup->getNumNeurons();
    }

    // If connectivitity initialisation snippet provides a function to calculate row length, call it
    // **NOTE** only do this for sparse connectivity as this should not be set for bitmasks
    auto calcMaxColLengthFunc = m_ConnectivityInitialiser.getSnippet()->getCalcMaxColLengthFunc();
    if(calcMaxColLengthFunc && (m_MatrixType & SynapseMatrixConnectivity::SPARSE)) {
        m_MaxSourceConnections = calcMaxColLengthFunc(srcNeuronGroup->getNumNeurons(), trgNeuronGroup->getNumNeurons(),
                                                      m_ConnectivityInitialiser.getParams());
    }
    // Otherwise, default to the size of the source population
    else {
        m_MaxSourceConnections = srcNeuronGroup->getNumNeurons();
    }

    // Check that the source neuron group supports the desired number of delay steps
    srcNeuronGroup->checkNumDelaySlots(delaySteps);
}
//----------------------------------------------------------------------------
void SynapseGroup::initDerivedParams(double dt)
{
    auto wuDerivedParams = getWUModel()->getDerivedParams();
    auto psDerivedParams = getPSModel()->getDerivedParams();

    // Reserve vector to hold derived parameters
    m_WUDerivedParams.reserve(wuDerivedParams.size());
    m_PSDerivedParams.reserve(psDerivedParams.size());

    // Loop through WU derived parameters
    for(const auto &d : wuDerivedParams) {
        m_WUDerivedParams.push_back(d.func(m_WUParams, dt));
    }

    // Loop through PSM derived parameters
    for(const auto &d : psDerivedParams) {
        m_PSDerivedParams.push_back(d.func(m_PSParams, dt));
    }

    // Initialise derived parameters for WU variable initialisers
    for(auto &v : m_WUVarInitialisers) {
        v.initDerivedParams(dt);
    }

    // Initialise derived parameters for PSM variable initialisers
    for(auto &v : m_PSVarInitialisers) {
        v.initDerivedParams(dt);
    }

    // Initialise derived parameters for WU presynaptic variable initialisers
    for(auto &v : m_WUPreVarInitialisers) {
        v.initDerivedParams(dt);
    }
    
    // Initialise derived parameters for WU postsynaptic variable initialisers
    for(auto &v : m_WUPostVarInitialisers) {
        v.initDerivedParams(dt);
    }

    // Initialise any derived connectivity initialiser parameters
    m_ConnectivityInitialiser.initDerivedParams(dt);
}
//----------------------------------------------------------------------------
std::string SynapseGroup::getSparseIndType() const
{
    // If narrow sparse inds are enabled
    if(m_NarrowSparseIndEnabled) {
        // If number of target neurons can be represented using a uint8, use this type
        const unsigned int numTrgNeurons = getTrgNeuronGroup()->getNumNeurons();
        if(numTrgNeurons <= std::numeric_limits<uint8_t>::max()) {
            return "uint8_t";
        }
        // Otherwise, if they can be represented as a uint16, use this type
        else if(numTrgNeurons <= std::numeric_limits<uint16_t>::max()) {
            return "uint16_t";
        }
    }

    // Otherwise, use 32-bit int
    return "uint32_t";

}
//----------------------------------------------------------------------------
bool SynapseGroup::canPSBeLinearlyCombined() const
{
    // Return true if there are no variables or extra global parameters
    // **NOTE** many models with variables would work fine, but  
    // nothing stops initialisers being used to configure PS models 
    // to behave totally different, similarly with EGPs
    return (getPSVarInitialisers().empty() && getPSModel()->getExtraGlobalParams().empty());
}
//----------------------------------------------------------------------------
bool SynapseGroup::canWUMPreUpdateBeCombined() const
{
    // If any presynaptic variables aren't initialised to constant values, this synapse group's presynaptic update can't be merged
    // **NOTE** hash check will compare these constant values
    if(std::any_of(getWUPreVarInitialisers().cbegin(), getWUPreVarInitialisers().cend(), 
                   [](const Models::VarInit &v){ return (dynamic_cast<const InitVarSnippet::Constant*>(v.getSnippet()) == nullptr); }))
    {
        return false;
    }
    
    // Loop through EGPs
    const auto wumEGPs = getWUModel()->getExtraGlobalParams();
    const std::string preSpikeCode = getWUModel()->getPreSpikeCode();
    const std::string preDynamicsCode = getWUModel()->getPreDynamicsCode();
    for(const auto &egp : wumEGPs) {
        // If this EGP is referenced in presynaptic spike code, return false
        const std::string egpName = "$(" + egp.name + ")";
        if(preSpikeCode.find(egpName) != std::string::npos) {
            return false;
        }
        
        // If this EGP is referenced in presynaptic dynamics code, return false
        if(preDynamicsCode.find(egpName) != std::string::npos) {
            return false;
        }
    }
    return true;
}
//----------------------------------------------------------------------------
bool SynapseGroup::canWUMPostUpdateBeCombined() const
{
    // If any postsynaptic variables aren't initialised to constant values, this synapse group's postsynaptic update can't be merged
    // **NOTE** hash check will compare these constant values
    if(std::any_of(getWUPostVarInitialisers().cbegin(), getWUPostVarInitialisers().cend(), 
                   [](const Models::VarInit &v){ return (dynamic_cast<const InitVarSnippet::Constant*>(v.getSnippet()) == nullptr); }))
    {
        return false;
    }
    
    // Loop through EGPs
    const auto wumEGPs = getWUModel()->getExtraGlobalParams();
    const std::string postSpikeCode = getWUModel()->getPostSpikeCode();
    const std::string postDynamicsCode = getWUModel()->getPostDynamicsCode();
    for(const auto &egp : wumEGPs) {
        // If this EGP is referenced in postsynaptic spike code, return false
        const std::string egpName = "$(" + egp.name + ")";
        if(postSpikeCode.find(egpName) != std::string::npos) {
            return false;
        }
        
        // If this EGP is referenced in postsynaptic dynamics code, return false
        if(postDynamicsCode.find(egpName) != std::string::npos) {
            return false;
        }
    }
    return true;
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getWUHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getWUModel()->getHashDigest(), hash);
    Utils::updateHash(getDelaySteps(), hash);
    Utils::updateHash(getBackPropDelaySteps(), hash);
    Utils::updateHash(getMaxDendriticDelayTimesteps(), hash);
    Utils::updateHash(getSparseIndType(), hash);
    Utils::updateHash(getNumThreadsPerSpike(), hash);
    Utils::updateHash(isEventThresholdReTestRequired(), hash);
    Utils::updateHash(getSpanType(), hash);
    Utils::updateHash(isPSModelMerged(), hash);
    Utils::updateHash(getSrcNeuronGroup()->getNumDelaySlots(), hash);
    Utils::updateHash(getTrgNeuronGroup()->getNumDelaySlots(), hash);
    Utils::updateHash(getMatrixType(), hash);

    // If weights are procedural, include variable initialiser hashes
    if(getMatrixType() & SynapseMatrixWeight::PROCEDURAL) {
        for(const auto &w : getWUVarInitialisers()) {
            Utils::updateHash(w.getHashDigest(), hash);
        }
    }

    // If connectivity is procedural, include connectivitiy initialiser hash
    if(getMatrixType() & SynapseMatrixConnectivity::PROCEDURAL) {
        Utils::updateHash(getConnectivityInitialiser().getHashDigest(), hash);
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
    Utils::updateHash((getDelaySteps() != 0), hash);
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getPSHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getPSModel()->getHashDigest(), hash);
    Utils::updateHash(getMaxDendriticDelayTimesteps(), hash);
    Utils::updateHash((getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM), hash);
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getPSLinearCombineHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getPSModel()->getHashDigest(), hash);
    Utils::updateHash(getMaxDendriticDelayTimesteps(), hash);
    Utils::updateHash((getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM), hash);
    Utils::updateHash(getPSParams(), hash);
    Utils::updateHash(getPSDerivedParams(), hash);
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
    Utils::updateHash(getSparseIndType(), hash);
    Utils::updateHash(getWUModel()->getVars(), hash);

    // Include variable initialiser hashes
    for(const auto &w : getWUVarInitialisers()) {
        Utils::updateHash(w.getHashDigest(), hash);
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
        Utils::updateHash(w.getHashDigest(), hash);
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
        Utils::updateHash(w.getHashDigest(), hash);
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
        Utils::updateHash(p.getHashDigest(), hash);
    }
    return hash.get_digest();
}
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type SynapseGroup::getConnectivityInitHashDigest() const
{
    boost::uuids::detail::sha1 hash;
    Utils::updateHash(getConnectivityInitialiser().getHashDigest(), hash);
    Utils::updateHash(getMatrixType(), hash);
    Utils::updateHash(getSparseIndType(), hash);
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
