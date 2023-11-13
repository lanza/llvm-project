#include <llvm/CodeGen/CommandFlags.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IRPrinter/IRPrintingPasses.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/InitializePasses.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <memory>
#include <optional>

using namespace llvm;

const static codegen::RegisterCodeGenFlags cfg;

static cl::opt<std::string> inputFilename(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"));

static cl::opt<std::string> outputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"));

static cl::opt<std::string> passPipeline("passes",
                                         cl::desc("The list of passes to run"));

static auto getCodeGenOptLevel() -> CodeGenOptLevel {
  return CodeGenOptLevel::None;
}

void realMain(Module &module, std::string_view passPipeline);

// Returns the TargetMachine instance or zero if no triple is provided.
static auto getTargetMachine(Triple theTriple, StringRef cpuStr,
                             StringRef featuresStr,
                             const TargetOptions &options) -> TargetMachine * {
  std::string error;
  const Target *theTarget =
      TargetRegistry::lookupTarget(codegen::getMArch(), theTriple, error);
  // Some modules don't specify a triple, and this is okay.
  if (theTarget == nullptr) {
    return nullptr;
  }

  return theTarget->createTargetMachine(
      theTriple.getTriple(), codegen::getCPUStr(), codegen::getFeaturesStr(),
      options, codegen::getExplicitRelocModel(),
      codegen::getExplicitCodeModel(), getCodeGenOptLevel());
}

auto main(int argc, char const **argv) -> int {
  InitLLVM llvmInit(argc, argv);

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
  PassRegistry &registry = *PassRegistry::getPassRegistry();
  initializeCore(registry);
  initializeScalarOpts(registry);
  initializeVectorization(registry);
  initializeIPO(registry);
  initializeAnalysis(registry);
  initializeTransformUtils(registry);
  initializeInstCombine(registry);
  initializeTarget(registry);
  // For codegen passes, only passes that do IR to IR transformation are
  // supported.
  initializeExpandLargeDivRemLegacyPassPass(registry);
  initializeExpandLargeFpConvertLegacyPassPass(registry);
  initializeExpandMemCmpPassPass(registry);
  initializeScalarizeMaskedMemIntrinLegacyPassPass(registry);
  initializeSelectOptimizePass(registry);
  initializeCallBrPreparePass(registry);
  initializeCodeGenPreparePass(registry);
  initializeAtomicExpandPass(registry);
  //initializeRewriteSymbolsLegacyPassPass(registry);
  initializeWinEHPreparePass(registry);
  initializeDwarfEHPrepareLegacyPassPass(registry);
  initializeSafeStackLegacyPassPass(registry);
  initializeSjLjEHPreparePass(registry);
  initializePreISelIntrinsicLoweringLegacyPassPass(registry);
  initializeGlobalMergePass(registry);
  initializeIndirectBrExpandPassPass(registry);
  initializeInterleavedLoadCombinePass(registry);
  initializeInterleavedAccessPass(registry);
  initializeUnreachableBlockElimLegacyPassPass(registry);
  initializeExpandReductionsPass(registry);
  initializeExpandVectorPredicationPass(registry);
  initializeWasmEHPreparePass(registry);
  initializeWriteBitcodePassPass(registry);
  initializeReplaceWithVeclibLegacyPass(registry);
  initializeJMCInstrumenterPass(registry);

  cl::ParseCommandLineOptions(argc, argv, "my optimizer\n");

  SMDiagnostic errors;
  LLVMContext context;
  context.setDiscardValueNames(false);

  std::unique_ptr<Module> module = parseIRFile(inputFilename, errors, context);
  if (!module) {
    errors.print(argv[0], errs());
    return 1;
  }

  std::unique_ptr<ToolOutputFile> out;

  if (outputFilename.empty())
    outputFilename = "-";

  std::error_code errorCode;
  sys::fs::OpenFlags flags = sys::fs::OF_TextWithCRLF;
  out = std::make_unique<ToolOutputFile>(outputFilename, errorCode, flags);
  if (errorCode) {
    errs() << errorCode.message() << '\n';
    return 1;
  }

  Triple moduleTriple(module->getTargetTriple());
  std::string cpuStr;
  std::string featuresStr;
  TargetMachine *machine = nullptr;
  const TargetOptions options =
      codegen::InitTargetOptionsFromCodeGenFlags(moduleTriple);

  if (moduleTriple.getArch() != 0U) {
    cpuStr = codegen::getCPUStr();
    featuresStr = codegen::getFeaturesStr();
    machine = getTargetMachine(moduleTriple, cpuStr, featuresStr, options);
  } else if (moduleTriple.getArchName() != "unknown" &&
             !moduleTriple.getArchName().empty()) {
    errs() << argv[0] << ": unrecognized architecture '"
           << moduleTriple.getArchName() << "' provided.\n";
    return 1;
  }

  std::unique_ptr<TargetMachine> targetMachine(machine);
  std::optional<PGOOptions> pgoOptions;
  PassInstrumentationCallbacks passInstrumentationCallbacks;
  PipelineTuningOptions pipelineTuningOptions;
  PassBuilder passBuilder(targetMachine.get(), pipelineTuningOptions,
                          pgoOptions, &passInstrumentationCallbacks);
  ModulePassManager outputPipeline;
  ModuleAnalysisManager mam;
  passBuilder.registerModuleAnalyses(mam);

  realMain(*module, passPipeline);

  outputPipeline.addPass(PrintModulePass(out->os(), "", true, false));
  outputPipeline.run(*module, mam);
  out->keep();
}
