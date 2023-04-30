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

#include <optional>

using namespace llvm;

static codegen::RegisterCodeGenFlags CFG;

static cl::opt<std::string> inputFilename(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"));

static cl::opt<std::string> outputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"));

static CodeGenOpt::Level GetCodeGenOptLevel() {
  return static_cast<CodeGenOpt::Level>(unsigned(0));
}

void realMain(Module &module);

// Returns the TargetMachine instance or zero if no triple is provided.
static TargetMachine *GetTargetMachine(Triple TheTriple, StringRef CPUStr,
                                       StringRef FeaturesStr,
                                       const TargetOptions &Options) {
  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(codegen::getMArch(), TheTriple, Error);
  // Some modules don't specify a triple, and this is okay.
  if (!TheTarget) {
    return nullptr;
  }

  return TheTarget->createTargetMachine(
      TheTriple.getTriple(), codegen::getCPUStr(), codegen::getFeaturesStr(),
      Options, codegen::getExplicitRelocModel(),
      codegen::getExplicitCodeModel(), GetCodeGenOptLevel());
}

int main(int argc, char const **argv) {
  InitLLVM x(argc, argv);

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeScalarOpts(Registry);
  initializeVectorization(Registry);
  initializeIPO(Registry);
  initializeAnalysis(Registry);
  initializeTransformUtils(Registry);
  initializeInstCombine(Registry);
  initializeTarget(Registry);
  // For codegen passes, only passes that do IR to IR transformation are
  // supported.
  initializeExpandLargeDivRemLegacyPassPass(Registry);
  initializeExpandLargeFpConvertLegacyPassPass(Registry);
  initializeExpandMemCmpPassPass(Registry);
  initializeScalarizeMaskedMemIntrinLegacyPassPass(Registry);
  initializeSelectOptimizePass(Registry);
  initializeCallBrPreparePass(Registry);
  initializeCodeGenPreparePass(Registry);
  initializeAtomicExpandPass(Registry);
  initializeRewriteSymbolsLegacyPassPass(Registry);
  initializeWinEHPreparePass(Registry);
  initializeDwarfEHPrepareLegacyPassPass(Registry);
  initializeSafeStackLegacyPassPass(Registry);
  initializeSjLjEHPreparePass(Registry);
  initializePreISelIntrinsicLoweringLegacyPassPass(Registry);
  initializeGlobalMergePass(Registry);
  initializeIndirectBrExpandPassPass(Registry);
  initializeInterleavedLoadCombinePass(Registry);
  initializeInterleavedAccessPass(Registry);
  initializeUnreachableBlockElimLegacyPassPass(Registry);
  initializeExpandReductionsPass(Registry);
  initializeExpandVectorPredicationPass(Registry);
  initializeWasmEHPreparePass(Registry);
  initializeWriteBitcodePassPass(Registry);
  initializeReplaceWithVeclibLegacyPass(Registry);
  initializeJMCInstrumenterPass(Registry);

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

  std::error_code ec;
  sys::fs::OpenFlags flags = sys::fs::OF_TextWithCRLF;
  out.reset(new ToolOutputFile(outputFilename, ec, flags));
  if (ec) {
    errs() << ec.message() << '\n';
    return 1;
  }

  Triple ModuleTriple(module->getTargetTriple());
  std::string CPUStr, FeaturesStr;
  TargetMachine *Machine = nullptr;
  const TargetOptions Options =
      codegen::InitTargetOptionsFromCodeGenFlags(ModuleTriple);

  if (ModuleTriple.getArch()) {
    CPUStr = codegen::getCPUStr();
    FeaturesStr = codegen::getFeaturesStr();
    Machine = GetTargetMachine(ModuleTriple, CPUStr, FeaturesStr, Options);
  } else if (ModuleTriple.getArchName() != "unknown" &&
             ModuleTriple.getArchName() != "") {
    errs() << argv[0] << ": unrecognized architecture '"
           << ModuleTriple.getArchName() << "' provided.\n";
    return 1;
  }

  std::unique_ptr<TargetMachine> tm(Machine);
  std::optional<PGOOptions> p;
  PassInstrumentationCallbacks pic;
  PipelineTuningOptions pto;
  PassBuilder PB(tm.get(), pto, p, &pic);
  ModulePassManager outputPipeline;
  ModuleAnalysisManager MAM;
  PB.registerModuleAnalyses(MAM);

  realMain(*module);

  outputPipeline.addPass(PrintModulePass(out->os(), "", true, false));
  outputPipeline.run(*module, MAM);
  out->keep();
}
