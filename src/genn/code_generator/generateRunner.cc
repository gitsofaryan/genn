#include "code_generator/generateRunner.h"

// Standard C++ includes
#include <sstream>
#include <string>

// GeNN includes
#include "modelSpec.h"

// GeNN code generator
#include "code_generator/codeStream.h"
#include "code_generator/teeStream.h"
#include "code_generator/backendBase.h"
#include "code_generator/codeGenUtils.h"

//--------------------------------------------------------------------------
// Anonymous namespace
//--------------------------------------------------------------------------
namespace
{
void writeTypeRange(CodeGenerator::CodeStream &os, const std::string &precision, const std::string &prefix)
{
    using namespace CodeGenerator;

    os << "#define " << prefix << "_MIN ";
    if (precision == "float") {
        writePreciseString(os, std::numeric_limits<float>::min());
        os << "f" << std::endl;
    }
    else {
        writePreciseString(os, std::numeric_limits<double>::min());
        os << std::endl;
    }

    os << "#define " << prefix << "_MAX ";
    if (precision == "float") {
        writePreciseString(os, std::numeric_limits<float>::max());
        os << "f" << std::endl;
    }
    else {
        writePreciseString(os, std::numeric_limits<double>::max());
        os << std::endl;
    }
    os << std::endl;
}
//-------------------------------------------------------------------------
void writeSpikeMacros(CodeGenerator::CodeStream &os, const NeuronGroup &ng, bool trueSpike)
{
    const bool delayRequired = trueSpike
        ? (ng.isDelayRequired() && ng.isTrueSpikeRequired())
        : ng.isDelayRequired();
    const std::string eventSuffix = trueSpike ? "" : "Evnt";
    const std::string eventMacroSuffix = trueSpike ? "" : "Event";

    // convenience macros for accessing spike count
    os << "#define spike" << eventMacroSuffix << "Count_" << ng.getName() << " glbSpkCnt" << eventSuffix << ng.getName();
    if (delayRequired) {
        os << "[spkQuePtr" << ng.getName() << "]";
    }
    else {
        os << "[0]";
    }
    os << std::endl;

    // convenience macro for accessing spikes
    os << "#define spike" << eventMacroSuffix << "_" << ng.getName();
    if (delayRequired) {
        os << " (glbSpk" << eventSuffix << ng.getName() << " + (spkQuePtr" << ng.getName() << "*" << ng.getNumNeurons() << "))";
    }
    else {
        os << " glbSpk" << eventSuffix << ng.getName();
    }
    os << std::endl;

    // convenience macro for accessing delay offset
    // **NOTE** we only require one copy of this so only ever write one for true spikes
    if(trueSpike) {
        os << "#define glbSpkShift" << ng.getName() << " ";
        if (delayRequired) {
            os << "spkQuePtr" << ng.getName() << "*" << ng.getNumNeurons();
        }
        else {
            os << "0";
        }
    }

    os << std::endl << std::endl;
}
//-------------------------------------------------------------------------
void genVarPushPullScope(CodeGenerator::CodeStream &definitionsFunc, CodeGenerator::CodeStream &runnerPushFunc, CodeGenerator::CodeStream &runnerPullFunc,
                         const std::string &description, bool unitialisedLogic,
                         std::function<void()> handler)
{
    definitionsFunc << "EXPORT_FUNC void push" << description << "ToDevice(";
    if(unitialisedLogic) {
        definitionsFunc << "bool uninitialisedOnly = false";
    }
    definitionsFunc << ");" << std::endl;
    definitionsFunc << "EXPORT_FUNC void pull" << description << "FromDevice();" << std::endl;

    runnerPushFunc << "void push" << description << "ToDevice(";
    if(unitialisedLogic) {
        runnerPushFunc << "bool uninitialisedOnly";
    }
    runnerPushFunc << ")";
    runnerPullFunc << "void pull" << description << "FromDevice()";
    {
        CodeGenerator::CodeStream::Scope a(runnerPushFunc);
        CodeGenerator::CodeStream::Scope b(runnerPullFunc);

        handler();
    }
    runnerPushFunc << std::endl;
    runnerPullFunc << std::endl;
}
}   // Anonymous namespace

//--------------------------------------------------------------------------
// CodeGenerator
//--------------------------------------------------------------------------
void CodeGenerator::generateRunner(CodeStream &definitions, CodeStream &definitionsInternal, CodeStream &runner, const NNmodel &model,
                                   const BackendBase &backend, int localHostID)
{
    // Write definitions preamble
    definitions << "#pragma once" << std::endl;

#ifdef _WIN32
    definitions << "#ifdef BUILDING_GENERATED_CODE" << std::endl;
    definitions << "#define EXPORT_VAR __declspec(dllexport) extern" << std::endl;
    definitions << "#define EXPORT_FUNC __declspec(dllexport)" << std::endl;
    definitions << "#else" << std::endl;
    definitions << "#define EXPORT_VAR __declspec(dllimport) extern" << std::endl;
    definitions << "#define EXPORT_FUNC __declspec(dllimport)" << std::endl;
    definitions << "#endif" << std::endl;
#else
    definitions << "#define EXPORT_VAR extern" << std::endl;
    definitions << "#define EXPORT_FUNC" << std::endl;
#endif
    backend.genDefinitionsPreamble(definitions);

    // Write definitions internal preamble
    definitionsInternal << "#pragma once" << std::endl;
    definitionsInternal << "#include \"definitions.h\"" << std::endl << std::endl;
    backend.genDefinitionsInternalPreamble(definitionsInternal);
    
    // write DT macro
    if (model.getTimePrecision() == "float") {
        definitions << "#define DT " << std::to_string(model.getDT()) << "f" << std::endl;
    } else {
        definitions << "#define DT " << std::to_string(model.getDT()) << std::endl;
    }

    // Typedefine scalar type
    definitions << "typedef " << model.getPrecision() << " scalar;" << std::endl;

    // Write ranges of scalar and time types
    writeTypeRange(definitions, model.getPrecision(), "SCALAR");
    writeTypeRange(definitions, model.getTimePrecision(), "TIME");

    definitions << "// ------------------------------------------------------------------------" << std::endl;
    definitions << "// bit tool macros" << std::endl;
    definitions << "#define B(x,i) ((x) & (0x80000000 >> (i))) //!< Extract the bit at the specified position i from x" << std::endl;
    definitions << "#define setB(x,i) x= ((x) | (0x80000000 >> (i))) //!< Set the bit at the specified position i in x to 1" << std::endl;
    definitions << "#define delB(x,i) x= ((x) & (~(0x80000000 >> (i)))) //!< Set the bit at the specified position i in x to 0" << std::endl;
    definitions << std::endl;

    // Write runner preamble
    runner << "#include \"definitionsInternal.h\"" << std::endl << std::endl;
    backend.genRunnerPreamble(runner);

    // Create codestreams to generate different sections of runner and definitions
    std::stringstream runnerVarDeclStream;
    std::stringstream runnerVarAllocStream;
    std::stringstream runnerVarFreeStream;
    std::stringstream runnerPushFuncStream;
    std::stringstream runnerPullFuncStream;
    std::stringstream definitionsVarStream;
    std::stringstream definitionsFuncStream;
    CodeStream runnerVarDecl(runnerVarDeclStream);
    CodeStream runnerVarAlloc(runnerVarAllocStream);
    CodeStream runnerVarFree(runnerVarFreeStream);
    CodeStream runnerPushFunc(runnerPushFuncStream);
    CodeStream runnerPullFunc(runnerPullFuncStream);
    CodeStream definitionsVar(definitionsVarStream);
    CodeStream definitionsFunc(definitionsFuncStream);

    // Create a teestream to allow simultaneous writing to all streams
    TeeStream allVarStreams(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree);

    // Begin extern C block around variable declarations
    runnerVarDecl << "extern \"C\" {" << std::endl;
    definitionsVar << "extern \"C\" {" << std::endl;
    definitionsInternal << "extern \"C\" {" << std::endl;

    allVarStreams << "// ------------------------------------------------------------------------" << std::endl;
    allVarStreams << "// global variables" << std::endl;
    allVarStreams << std::endl;

    // Define and declare time variables
    definitionsVar << "EXPORT_VAR unsigned long long iT;" << std::endl;
    definitionsVar << "EXPORT_VAR " << model.getTimePrecision() << " t;" << std::endl;
    runnerVarDecl << "unsigned long long iT;" << std::endl;
    runnerVarDecl << model.getTimePrecision() << " t;" << std::endl;

    // If backend requires a global RNG to simulate (or initialize) this model
    if(backend.isGlobalRNGRequired(model)) {
        backend.genGlobalRNG(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, model);
    }

    //---------------------------------
    // REMOTE NEURON GROUPS
    allVarStreams << "// ------------------------------------------------------------------------" << std::endl;
    allVarStreams << "// remote neuron groups" << std::endl;
    allVarStreams << std::endl;

    // Loop through remote neuron groups
    for(const auto &n : model.getRemoteNeuronGroups()) {
        // Write macro so whether a neuron group is remote or not can be determined at compile time
        // **NOTE** we do this for REMOTE groups so #ifdef GROUP_NAME_REMOTE is backward compatible
        definitionsVar << "#define " << n.first << "_REMOTE" << std::endl;

        // Write convenience macros to access spikes
        writeSpikeMacros(definitionsVar, n.second, true);

        // If this neuron group has outputs to local host
        if(n.second.hasOutputToHost(localHostID)) {
            // Check that, whatever variable mode is set for these variables,
            // they are instantiated on host so they can be copied using MPI
            if(!(n.second.getSpikeLocation() & VarLocation::HOST)) {
                throw std::runtime_error("Remote neuron group '" + n.first + "' has its spike variable mode set so it is not instantiated on the host - this is not supported");
            }

            // True spike variable
            genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, n.first + "Spikes", true,
                [&]()
                {
                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        "unsigned int", "glbSpkCnt" + n.first, n.second.getSpikeLocation(), true,
                                        n.second.isTrueSpikeRequired() ? n.second.getNumDelaySlots() : 1);
                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        "unsigned int", "glbSpk" + n.first, n.second.getSpikeLocation(), true,
                                        n.second.isTrueSpikeRequired() ? n.second.getNumNeurons() * n.second.getNumDelaySlots() : n.second.getNumNeurons());
                });
        }
    }
    allVarStreams << std::endl;

    //---------------------------------
    // LOCAL NEURON VARIABLES
    allVarStreams << "// ------------------------------------------------------------------------" << std::endl;
    allVarStreams << "// local neuron groups" << std::endl;
    allVarStreams << std::endl;

    for(const auto &n : model.getLocalNeuronGroups()) {
        // Write convenience macros to access spikes
        writeSpikeMacros(definitionsVar, n.second, true);

        // True spike variable
        genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, n.first + "Spikes", true,
            [&]()
            {
                backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                    "unsigned int", "glbSpkCnt" + n.first, n.second.getSpikeLocation(), 
                                    true, n.second.isTrueSpikeRequired() ? n.second.getNumDelaySlots() : 1);
                backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                    "unsigned int", "glbSpk" + n.first, n.second.getSpikeLocation(),
                                    true, n.second.isTrueSpikeRequired() ? n.second.getNumNeurons() * n.second.getNumDelaySlots() : n.second.getNumNeurons());
            });
        
        genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, n.first + "CurrentSpikes", false,
            [&]()
            {
                backend.genCurrentTrueSpikePush(runnerPushFunc, n.second);
                backend.genCurrentTrueSpikePull(runnerPullFunc, n.second);
            });

        // If neuron ngroup eeds to emit spike-like events
        if (n.second.isSpikeEventRequired()) {
            // Write convenience macros to access spike-like events
            writeSpikeMacros(definitionsVar, n.second, false);

            genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, n.first + "SpikeEvents", true,
                [&]()
                {
                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        "unsigned int", "glbSpkCntEvnt" + n.first, n.second.getSpikeEventLocation(),
                                        true, n.second.getNumDelaySlots());
                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        "unsigned int", "glbSpkEvnt" + n.first, n.second.getSpikeEventLocation(),
                                        true, n.second.getNumNeurons() * n.second.getNumDelaySlots());
                });

            genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, n.first + "CurrentSpikeEvents", false,
                [&]()
                {
                    backend.genCurrentSpikeLikeEventPush(runnerPushFunc, n.second);
                    backend.genCurrentSpikeLikeEventPull(runnerPullFunc, n.second);
                });
        }

        // If neuron group has axonal delays
        if (n.second.isDelayRequired()) {
            backend.genScalar(definitionsVar, definitionsInternal, runnerVarDecl, "unsigned int", "spkQuePtr" + n.first);
        }

        // If neuron group needs to record its spike times
        if (n.second.isSpikeTimeRequired()) {
            backend.genArray(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, model.getTimePrecision(), "sT" + n.first, n.second.getSpikeTimeLocation(),
                             n.second.getNumNeurons() * n.second.getNumDelaySlots());
        }

        // If neuron group needs per-neuron RNGs
        if(n.second.isSimRNGRequired()) {
            backend.genPopulationRNG(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, "rng" + n.first, n.second.getNumNeurons());
        }

        // Neuron state variables
        const auto neuronModel = n.second.getNeuronModel();
        genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, n.first + "State", true,
            [&]()
            {
                const auto vars = neuronModel->getVars();
                for(size_t i = 0; i < vars.size(); i++) {
                    const size_t count = n.second.isVarQueueRequired(i) ? n.second.getNumNeurons() * n.second.getNumDelaySlots() : n.second.getNumNeurons();
                    const bool autoInitialized = !n.second.getVarInitialisers()[i].getSnippet()->getCode().empty();

                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        vars[i].second, vars[i].first + n.first, n.second.getVarLocation(i), autoInitialized, count);
                }
            });

        for(auto const &v : neuronModel->getExtraGlobalParams()) {
            definitionsVar << "extern " << v.second << " " << v.first + n.first << ";" << std::endl;
            runnerVarDecl << v.second << " " <<  v.first << n.first << ";" << std::endl;
        }

        if(!n.second.getCurrentSources().empty()) {
            allVarStreams << "// current source variables" << std::endl;
        }
        for (auto const *cs : n.second.getCurrentSources()) {
            const auto csModel = cs->getCurrentSourceModel();
            const auto csVars = csModel->getVars();
            genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, cs->getName() + "State", true,
                [&]()
                {
                    for(size_t i = 0; i < csVars.size(); i++) {
                        const bool autoInitialized = !n.second.getVarInitialisers()[i].getSnippet()->getCode().empty();

                        backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                            csVars[i].second, csVars[i].first + cs->getName(), cs->getVarLocation(i), autoInitialized, n.second.getNumNeurons());

                    }
                });

            for(auto const &v : csModel->getExtraGlobalParams()) {
                definitionsVar << "extern " << v.second << " " <<  v.first << cs->getName() << ";" << std::endl;
                runnerVarDecl << v.second << " " <<  v.first << cs->getName() << ";" << std::endl;
            }
        }
    }
    allVarStreams << std::endl;

    //----------------------------------
    // POSTSYNAPTIC VARIABLES
    allVarStreams << "// ------------------------------------------------------------------------" << std::endl;
    allVarStreams << "// postsynaptic variables" << std::endl;
    allVarStreams << std::endl;
    for(const auto &n : model.getLocalNeuronGroups()) {
        // Loop through merged incoming synaptic populations
        // **NOTE** because of merging we need to loop through postsynaptic models in this
        for(const auto &m : n.second.getMergedInSyn()) {
            const auto *sg = m.first;

            backend.genArray(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, model.getPrecision(), "inSyn" + sg->getPSModelTargetName(), sg->getInSynLocation(),
                             sg->getTrgNeuronGroup()->getNumNeurons());

            if (sg->isDendriticDelayRequired()) {
                backend.genArray(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, model.getPrecision(), "denDelay" + sg->getPSModelTargetName(), sg->getDendriticDelayLocation(),
                                 sg->getMaxDendriticDelayTimesteps() * sg->getTrgNeuronGroup()->getNumNeurons());
                backend.genScalar(definitionsVar, definitionsInternal, runnerVarDecl, "unsigned int", "denDelayPtr" + sg->getPSModelTargetName());
            }

            if (sg->getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM) {
                for(const auto &v : sg->getPSModel()->getVars()) {
                    backend.genArray(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, v.second, v.first + sg->getPSModelTargetName(), sg->getPSVarLocation(v.first),
                                     sg->getTrgNeuronGroup()->getNumNeurons());
                }
            }

            for(auto const &v : sg->getPSModel()->getExtraGlobalParams()) {
                definitionsVar << "extern " << v.second << " " <<  v.first << sg->getPSModelTargetName() << ";" << std::endl;
                runnerVarDecl << v.second << " " <<  v.first << sg->getPSModelTargetName() << ";" << std::endl;
            }
        }
    }
    allVarStreams << std::endl;

    //----------------------------------
    // SYNAPSE CONNECTIVITY
    allVarStreams << "// ------------------------------------------------------------------------" << std::endl;
    allVarStreams << "// synapse connectivity" << std::endl;
    allVarStreams << std::endl;
    for(const auto &s : model.getLocalSynapseGroups()) {
        genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, s.first + "Connectivity", true,
            [&]()
            {
                const bool autoInitialized = !s.second.getConnectivityInitialiser().getSnippet()->getRowBuildCode().empty();

                if (s.second.getMatrixType() & SynapseMatrixConnectivity::BITMASK) {
                    const size_t gpSize = ((size_t)s.second.getSrcNeuronGroup()->getNumNeurons() * (size_t)s.second.getTrgNeuronGroup()->getNumNeurons()) / 32 + 1;
                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree,runnerPushFunc, runnerPullFunc,
                                        "uint32_t", "gp" + s.first, s.second.getSparseConnectivityLocation(), autoInitialized, gpSize);
                }
                else if(s.second.getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
                    const VarLocation varLoc = s.second.getSparseConnectivityLocation();
                    const size_t size = s.second.getSrcNeuronGroup()->getNumNeurons() * s.second.getMaxConnections();

                    // Maximum row length constant
                    definitionsVar << "extern const unsigned int maxRowLength" << s.first << ";" << std::endl;
                    runnerVarDecl << "const unsigned int maxRowLength" << s.first << " = " << s.second.getMaxConnections() << ";" << std::endl;

                    // Row lengths
                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        "unsigned int", "rowLength" + s.first, varLoc, autoInitialized, s.second.getSrcNeuronGroup()->getNumNeurons());

                    // Target indices
                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        "unsigned int", "ind" + s.first, varLoc, autoInitialized, size);

                    // **TODO** remap is not always required
                    if(!s.second.getWUModel()->getSynapseDynamicsCode().empty()) {
                        // Allocate synRemap
                        // **THINK** this is over-allocating
                        backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                            "unsigned int", "synRemap" + s.first, varLoc, autoInitialized, size + 1);
                    }

                    // **TODO** remap is not always required
                    if(!s.second.getWUModel()->getLearnPostCode().empty()) {
                        const size_t postSize = (size_t)s.second.getTrgNeuronGroup()->getNumNeurons() * (size_t)s.second.getMaxSourceConnections();

                        // Allocate column lengths
                        backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                            "unsigned int", "colLength" + s.first, varLoc, autoInitialized, s.second.getTrgNeuronGroup()->getNumNeurons());

                        // Allocate remap
                        backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                            "unsigned int", "remap" + s.first, varLoc, autoInitialized, postSize);
                    }
                }
            });
    }

    //----------------------------------
    // SYNAPSE VARIABLE
    allVarStreams << "// ------------------------------------------------------------------------" << std::endl;
    allVarStreams << "// synapse variables" << std::endl;
    allVarStreams << std::endl;
    for(const auto &s : model.getLocalSynapseGroups()) {
        const auto *wu = s.second.getWUModel();
        const auto *psm = s.second.getPSModel();

        genVarPushPullScope(definitionsFunc, runnerPushFunc, runnerPullFunc, s.first + "State", true,
            [&]()
            {
                // If weight update variables should be individual
                if (s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL) {
                    const size_t size = (s.second.getMatrixType() & SynapseMatrixConnectivity::DENSE)
                        ? s.second.getSrcNeuronGroup()->getNumNeurons() * s.second.getTrgNeuronGroup()->getNumNeurons()
                        : s.second.getSrcNeuronGroup()->getNumNeurons() * s.second.getMaxConnections();

                    const auto wuVars = wu->getVars();
                    for(size_t i = 0; i < wuVars.size(); i++) {
                        const bool autoInitialized = !s.second.getWUVarInitialisers()[i].getSnippet()->getCode().empty();

                        backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                            wuVars[i].second, wuVars[i].first + s.first, s.second.getWUVarLocation(i), autoInitialized, size);
                    }
                }

                // Presynaptic W.U.M. variables
                const size_t preSize = (s.second.getDelaySteps() == NO_DELAY)
                        ? s.second.getSrcNeuronGroup()->getNumNeurons()
                        : s.second.getSrcNeuronGroup()->getNumNeurons() * s.second.getSrcNeuronGroup()->getNumDelaySlots();
                const auto wuPreVars = wu->getPreVars();
                for(size_t i = 0; i < wuPreVars.size(); i++) {
                    const bool autoInitialized = !s.second.getWUPreVarInitialisers()[i].getSnippet()->getCode().empty();

                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        wuPreVars[i].second, wuPreVars[i].first + s.first, s.second.getWUPreVarLocation(i), autoInitialized, preSize);
                }

                // Postsynaptic W.U.M. variables
                const size_t postSize = (s.second.getBackPropDelaySteps() == NO_DELAY)
                        ? s.second.getTrgNeuronGroup()->getNumNeurons()
                        : s.second.getTrgNeuronGroup()->getNumNeurons() * s.second.getTrgNeuronGroup()->getNumDelaySlots();
                const auto wuPostVars = wu->getPostVars();
                for(size_t i = 0; i < wuPostVars.size(); i++) {
                    const bool autoInitialized = !s.second.getWUPostVarInitialisers()[i].getSnippet()->getCode().empty();

                    backend.genVariable(definitionsVar, definitionsInternal, runnerVarDecl, runnerVarAlloc, runnerVarFree, runnerPushFunc, runnerPullFunc,
                                        wuPostVars[i].second, wuPostVars[i].first + s.first, s.second.getWUPostVarLocation(i), autoInitialized, postSize);
                }

                // If this synapse group's postsynaptic models hasn't been merged (which makes pulling them somewhat ambiguous)
                // **NOTE** we generated initialisation and declaration code earlier - here we just generate push and pull as we want this per-synapse group
                if(!s.second.isPSModelMerged()) {
                    // Add code to push and pull inSyn
                    backend.genVariablePushPull(runnerPushFunc, runnerPullFunc, model.getPrecision(), "inSyn" + s.first,
                                                s.second.getInSynLocation(), true, s.second.getTrgNeuronGroup()->getNumNeurons());

                    // If this synapse group has individual postsynaptic model variables
                    if (s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM) {
                        const auto psmVars = psm->getVars();
                        for(size_t i = 0; i < psmVars.size(); i++) {
                            const bool autoInitialized = !s.second.getPSVarInitialisers()[i].getSnippet()->getCode().empty();
                            backend.genVariablePushPull(runnerPushFunc, runnerPullFunc, psmVars[i].second, psmVars[i].first + s.first,
                                                        s.second.getPSVarLocation(i), autoInitialized, s.second.getTrgNeuronGroup()->getNumNeurons());
                        }
                    }
                }
            });

        for(const auto &v : wu->getExtraGlobalParams()) {
            definitionsVar << "extern " << v.second << " " << v.first + s.first << ";" << std::endl;
            runnerVarDecl << v.second << " " <<  v.first << s.first << ";" << std::endl;
        }

        for(auto const &p : s.second.getConnectivityInitialiser().getSnippet()->getExtraGlobalParams()) {
            definitionsVar << "extern " << p.second << " initSparseConn" << p.first + s.first << ";" << std::endl;
            runnerVarDecl << p.second << " initSparseConn" << p.first + s.first << ";" << std::endl;
        }
    }
    allVarStreams << std::endl;

    // End extern C block around variable declarations
    runnerVarDecl << "}  // extern \"C\"" << std::endl;
 

    // Write variable declarations to runner
    runner << runnerVarDeclStream.str();

    // Write push function declarations to runner
    runner << "// ------------------------------------------------------------------------" << std::endl;
    runner << "// copying things to device" << std::endl;
    runner << runnerPushFuncStream.str();
    runner << std::endl;

    // Write pull function declarations to runner
    runner << "// ------------------------------------------------------------------------" << std::endl;
    runner << "// copying things from device" << std::endl;
    runner << runnerPullFuncStream.str();
    runner << std::endl;

    // ---------------------------------------------------------------------
    // Function for copying all state to device
    runner << "void copyStateToDevice(bool uninitialisedOnly)";
    {
        CodeStream::Scope b(runner);
         for(const auto &n : model.getLocalNeuronGroups()) {
            runner << "push" << n.first << "StateToDevice(uninitialisedOnly);" << std::endl;
        }

        for(const auto &cs : model.getLocalCurrentSources()) {
            runner << "push" << cs.first << "StateToDevice(uninitialisedOnly);" << std::endl;
        }

        for(const auto &s : model.getLocalSynapseGroups()) {
            runner << "push" << s.first << "StateToDevice(uninitialisedOnly);" << std::endl;
        }
    }
    runner << std::endl;

    // ---------------------------------------------------------------------
    // Function for copying all connectivity to device
    runner << "void copyConnectivityToDevice(bool uninitialisedOnly)";
    {
        CodeStream::Scope b(runner);
        for(const auto &s : model.getLocalSynapseGroups()) {
            runner << "push" << s.first << "ConnectivityToDevice(uninitialisedOnly);" << std::endl;
        }
    }
    runner << std::endl;

    // ---------------------------------------------------------------------
    // Function for copying all state from device
    runner << "void copyStateFromDevice()";
    {
        CodeStream::Scope b(runner);
         for(const auto &n : model.getLocalNeuronGroups()) {
            runner << "pull" << n.first << "StateFromDevice();" << std::endl;
        }

        for(const auto &cs : model.getLocalCurrentSources()) {
            runner << "pull" << cs.first << "StateFromDevice();" << std::endl;
        }

        for(const auto &s : model.getLocalSynapseGroups()) {
            runner << "pull" << s.first << "StateFromDevice();" << std::endl;
        }
    }
    runner << std::endl;

    // ---------------------------------------------------------------------
    // Function for setting the CUDA device and the host's global variables.
    // Also estimates memory usage on device ...
    runner << "void allocateMem()";
    {
        CodeStream::Scope b(runner);

        // Generate preamble -this is the first bit of generated code called by user simulations
        // so global initialisation is often performed here
        backend.genAllocateMemPreamble(runner, model);

        // Write variable allocations to runner
        runner << runnerVarAllocStream.str();
    }
    runner << std::endl;

    // ------------------------------------------------------------------------
    // Function to free all global memory structures
    runner << "void freeMem()";
    {
        CodeStream::Scope b(runner);

        // Write variable frees to runner
        runner << runnerVarFreeStream.str();
    }
    runner << std::endl;

    // ------------------------------------------------------------------------
    // Function to free all global memory structures
    runner << "void stepTime()";
    {
        CodeStream::Scope b(runner);

        // Update synaptic state
        runner << "updateSynapses(t);" << std::endl;

        // Generate code to advance host-side spike queues
        for(const auto &n : model.getRemoteNeuronGroups()) {
            if(n.second.isDelayRequired() && n.second.hasOutputToHost(localHostID)) {
                runner << "spkQuePtr" << n.first << " = (spkQuePtr" << n.first << " + 1) % " << n.second.getNumDelaySlots() << ";" << std::endl;
            }
        }
        for(const auto &n : model.getLocalNeuronGroups()) {
            if (n.second.isDelayRequired()) {
                runner << "spkQuePtr" << n.first << " = (spkQuePtr" << n.first << " + 1) % " << n.second.getNumDelaySlots() << ";" << std::endl;
            }
        }

        // Update neuronal state
        runner << "updateNeurons(t);" << std::endl;

        // Generate code to advance host side dendritic delay buffers
        for(const auto &n : model.getLocalNeuronGroups()) {
            // Loop through incoming synaptic populations
            for(const auto &m : n.second.getMergedInSyn()) {
                const auto *sg = m.first;
                if(sg->isDendriticDelayRequired()) {
                    runner << "denDelayPtr" << sg->getPSModelTargetName() << " = (denDelayPtr" << sg->getPSModelTargetName() << " + 1) % " << sg->getMaxDendriticDelayTimesteps() << ";" << std::endl;
                }
            }
        }
        // Advance time
        runner << "iT++;" << std::endl;
        runner << "t = iT*DT;" << std::endl;

        // Synchronise if zero-copy is in use
        // **TODO** move to backend
        if(model.zeroCopyInUse()) {
            runner << "cudaDeviceSynchronize();" << std::endl;
        }
    }
    runner << std::endl;

    // Write variable and function definitions to header
    definitions << definitionsVarStream.str();
    definitions << definitionsFuncStream.str();

    // ---------------------------------------------------------------------
    // Function definitions
    definitions << "// Runner functions" << std::endl;
    definitions << "EXPORT_FUNC void copyStateToDevice(bool uninitialisedOnly = false);" << std::endl;
    definitions << "EXPORT_FUNC void copyConnectivityToDevice(bool uninitialisedOnly = false);" << std::endl;
    definitions << "EXPORT_FUNC void copyStateFromDevice();" << std::endl;
    definitions << "EXPORT_FUNC void allocateMem();" << std::endl;
    definitions << "EXPORT_FUNC void freeMem();" << std::endl;
    definitions << "EXPORT_FUNC void stepTime();" << std::endl;
    definitions << std::endl;
    definitions << "// Functions generated by backend" << std::endl;
    definitions << "EXPORT_FUNC void updateNeurons(" << model.getTimePrecision() << " t);" << std::endl;
    definitions << "EXPORT_FUNC void updateSynapses(" << model.getTimePrecision() << " t);" << std::endl;
    definitions << "EXPORT_FUNC void initialize();" << std::endl;
    definitions << "EXPORT_FUNC void initializeSparse();" << std::endl;

    // End extern C block around definitions
    definitions << "}  // extern \"C\"" << std::endl;
    definitionsInternal << "}  // extern \"C\"" << std::endl;

}
