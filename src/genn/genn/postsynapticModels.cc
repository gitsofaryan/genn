#include "postsynapticModels.h"

// GeNN includes
#include "gennUtils.h"

using namespace GeNN;

namespace GeNN::PostsynapticModels
{
// Implement models
IMPLEMENT_SNIPPET(ExpCurr);
IMPLEMENT_SNIPPET(ExpCond);
IMPLEMENT_SNIPPET(DeltaCurr);

//----------------------------------------------------------------------------
// GeNN::PostsynapticModels::Base
//----------------------------------------------------------------------------
boost::uuids::detail::sha1::digest_type Base::getHashDigest() const
{
    // Superclass
    boost::uuids::detail::sha1 hash;
    Snippet::Base::updateHash(hash);
    Utils::updateHash(getVars(), hash);
    Utils::updateHash(getNeuronVarRefs(), hash);
    Utils::updateHash(getDecayCode(), hash);
    Utils::updateHash(getApplyInputCode(), hash);
    return hash.get_digest();
}
//----------------------------------------------------------------------------
void Base::validate(const std::unordered_map<std::string, double> &paramValues, 
                    const std::unordered_map<std::string, InitVarSnippet::Init> &varValues,
                    const std::unordered_map<std::string, Models::VarReference> &varRefTargets,
                    const std::string &description) const
{
    // Superclass
    Snippet::Base::validate(paramValues, description);

    // Validate variable names and initialisers
    const auto vars = getVars();
    Utils::validateVecNames(vars, "Variable");
    Utils::validateInitialisers(vars, varValues, "variable", description);

    // Validate variable reference initialisers
    const auto varRefs = getNeuronVarRefs();
    Utils::validateVecNames(varRefs, "Neuron variable reference");
    Utils::validateInitialisers(varRefs, varRefTargets, "Neuron variable reference", description);
}
}   // namespace GeNN::PostsynapticModels