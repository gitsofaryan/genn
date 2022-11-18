// Google test includes
#include "gtest/gtest.h"

// GeNN includes
#include "modelSpecInternal.h"

//--------------------------------------------------------------------------
// Anonymous namespace
//--------------------------------------------------------------------------
namespace
{
class AlphaCurr : public PostsynapticModels::Base
{
public:
    DECLARE_MODEL(AlphaCurr, 1, 1);

    SET_DECAY_CODE(
        "$(x) = (DT * $(expDecay) * $(inSyn) * $(init)) + ($(expDecay) * $(x));\n"
        "$(inSyn)*=$(expDecay);\n");

    SET_CURRENT_CONVERTER_CODE("$(x)");

    SET_PARAM_NAMES({"tau"});

    SET_VARS({{"x", "scalar"}});

    SET_DERIVED_PARAMS({
        {"expDecay", [](const std::vector<double> &pars, double dt) { return std::exp(-dt / pars[0]); }},
        {"init", [](const std::vector<double> &pars, double) { return (std::exp(1) / pars[0]); }}});
};
IMPLEMENT_MODEL(AlphaCurr);

class Sum : public CustomUpdateModels::Base
{
    DECLARE_CUSTOM_UPDATE_MODEL(Sum, 0, 1, 2);

    SET_UPDATE_CODE("$(sum) = $(a) + $(b);\n");

    SET_VARS({{"sum", "scalar"}});
    SET_VAR_REFS({{"a", "scalar", VarAccessMode::READ_ONLY}, 
                  {"b", "scalar", VarAccessMode::READ_ONLY}});
};
IMPLEMENT_MODEL(Sum);

class RemoveSynapse : public CustomConnectivityUpdateModels::Base
{
public:
    DECLARE_CUSTOM_CONNECTIVITY_UPDATE_MODEL(RemoveSynapse, 0, 1, 0, 0, 0, 0, 0);
    
    SET_VARS({{"a", "scalar"}});
    SET_ROW_UPDATE_CODE(
        "$(for_each_synapse,\n"
        "{\n"
        "   if($(id_post) == ($(id_pre) + 1)) {\n"
        "       $(remove_synapse);\n"
        "       break;\n"
        "   }\n"
        "});\n");
};
IMPLEMENT_MODEL(RemoveSynapse);
}

//--------------------------------------------------------------------------
// Tests
//--------------------------------------------------------------------------
TEST(ModelSpec, NeuronGroupZeroCopy)
{
    ModelSpecInternal model;

    NeuronModels::Izhikevich::ParamValues paramVals(0.02, 0.2, -65.0, 8.0);
    NeuronModels::Izhikevich::VarValues varVals(0.0, 0.0);
    NeuronGroup *ng = model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons0", 10, paramVals, varVals);
    ng->setSpikeLocation(VarLocation::HOST_DEVICE_ZERO_COPY);

    ASSERT_TRUE(model.zeroCopyInUse());
}
//--------------------------------------------------------------------------
TEST(ModelSpec, CurrentSourceZeroCopy)
{
    ModelSpecInternal model;

    NeuronModels::Izhikevich::ParamValues paramVals(0.02, 0.2, -65.0, 8.0);
    NeuronModels::Izhikevich::VarValues varVals(0.0, 0.0);
    model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons", 10, paramVals, varVals);

    CurrentSourceModels::PoissonExp::ParamValues csParamVals(0.1, 5.0, 10.0);
    CurrentSourceModels::PoissonExp::VarValues csVarVals(0.0);
    CurrentSource *cs = model.addCurrentSource<CurrentSourceModels::PoissonExp>("CS", "Neurons", csParamVals, csVarVals);
    cs->setVarLocation("current", VarLocation::HOST_DEVICE_ZERO_COPY);

    ASSERT_TRUE(model.zeroCopyInUse());
}
//--------------------------------------------------------------------------
TEST(ModelSpec, PSMZeroCopy)
{
    ModelSpecInternal model;

    NeuronModels::Izhikevich::ParamValues paramVals(0.02, 0.2, -65.0, 8.0);
    NeuronModels::Izhikevich::VarValues varVals(0.0, 0.0);
    model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons0", 10, paramVals, varVals);
    model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons1", 10, paramVals, varVals);

    SynapseGroup *sg = model.addSynapsePopulation<WeightUpdateModels::StaticPulse, AlphaCurr>(
        "Synapse", SynapseMatrixType::DENSE_INDIVIDUALG, NO_DELAY,
        "Neurons0", "Neurons1",
        {}, {1.0},
        {5.0}, {0.0});
    sg->setPSVarLocation("x", VarLocation::HOST_DEVICE_ZERO_COPY);

    ASSERT_TRUE(model.zeroCopyInUse());
}
//--------------------------------------------------------------------------
TEST(ModelSpec, WUZeroCopy)
{
    ModelSpecInternal model;

    NeuronModels::Izhikevich::ParamValues paramVals(0.02, 0.2, -65.0, 8.0);
    NeuronModels::Izhikevich::VarValues varVals(0.0, 0.0);
    model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons0", 10, paramVals, varVals);
    model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons1", 10, paramVals, varVals);

    SynapseGroup *sg = model.addSynapsePopulation<WeightUpdateModels::StaticPulse, PostsynapticModels::DeltaCurr>(
        "Synapse", SynapseMatrixType::DENSE_INDIVIDUALG, NO_DELAY,
        "Neurons0", "Neurons1",
        {}, {1.0},
        {}, {});
    sg->setWUVarLocation("g", VarLocation::HOST_DEVICE_ZERO_COPY);

    ASSERT_TRUE(model.zeroCopyInUse());
}
//--------------------------------------------------------------------------
TEST(ModelSpec, CustomUpdateZeroCopy)
{
    ModelSpecInternal model;

    NeuronModels::Izhikevich::ParamValues paramVals(0.02, 0.2, -65.0, 8.0);
    NeuronModels::Izhikevich::VarValues varVals(0.0, 0.0);
    NeuronGroup *ng = model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons", 10, paramVals, varVals);

    Sum::VarReferences varRefs(createVarRef(ng, "V"), createVarRef(ng, "U"));
    CustomUpdate *cu = model.addCustomUpdate<Sum>("Sum", "Test", 
                                                  {}, {0.0}, varRefs);
    cu->setVarLocation("sum", VarLocation::HOST_DEVICE_ZERO_COPY);
    ASSERT_TRUE(model.zeroCopyInUse());
}
//--------------------------------------------------------------------------
TEST(ModelSpec, CustomConnectivityUpdateZeroCopy)
{
    ModelSpecInternal model;

    NeuronModels::Izhikevich::ParamValues paramVals(0.02, 0.2, -65.0, 8.0);
    NeuronModels::Izhikevich::VarValues varVals(0.0, 0.0);
    model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons0", 10, paramVals, varVals);
    model.addNeuronPopulation<NeuronModels::Izhikevich>("Neurons1", 10, paramVals, varVals);

    SynapseGroup *sg = model.addSynapsePopulation<WeightUpdateModels::StaticPulseDendriticDelay, PostsynapticModels::DeltaCurr>(
        "Synapse", SynapseMatrixType::SPARSE_INDIVIDUALG, NO_DELAY,
        "Neurons0", "Neurons1",
        {}, {1.0, 1},
        {}, {});

    CustomConnectivityUpdate *cu = model.addCustomConnectivityUpdate<RemoveSynapse>("RemoveSynapse", "Test", "Synapse",
                                                                                    {}, {0.0}, {}, {},
                                                                                    {}, {}, {});
    cu->setVarLocation("a", VarLocation::HOST_DEVICE_ZERO_COPY);
    ASSERT_TRUE(model.zeroCopyInUse());
}