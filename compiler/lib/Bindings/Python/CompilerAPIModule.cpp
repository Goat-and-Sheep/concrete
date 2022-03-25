// Part of the Concrete Compiler Project, under the BSD3 License with Zama
// Exceptions. See
// https://github.com/zama-ai/concrete-compiler-internal/blob/master/LICENSE.txt
// for license information.

#include "CompilerAPIModule.h"
#include "concretelang-c/Support/CompilerEngine.h"
#include "concretelang/Dialect/FHE/IR/FHEOpsDialect.h.inc"
#include "concretelang/Support/Jit.h"
#include "concretelang/Support/JitLambdaSupport.h"
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/ExecutionEngine/OptUtils.h>
#include <mlir/Parser.h>

#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <stdexcept>
#include <string>

using mlir::concretelang::CompilationOptions;
using mlir::concretelang::JitLambdaSupport;
using mlir::concretelang::LambdaArgument;

/// Populate the compiler API python module.
void mlir::concretelang::python::populateCompilerAPISubmodule(
    pybind11::module &m) {
  m.doc() = "Concretelang compiler python API";

  m.def("round_trip",
        [](std::string mlir_input) { return roundTrip(mlir_input.c_str()); });

  m.def("terminate_parallelization", &terminateParallelization);

  pybind11::class_<CompilationOptions>(m, "CompilationOptions")
      .def(pybind11::init(
          [](std::string funcname) { return CompilationOptions(funcname); }))
      .def("set_funcname",
           [](CompilationOptions &options, std::string funcname) {
             options.clientParametersFuncName = funcname;
           })
      .def("set_verify_diagnostics",
           [](CompilationOptions &options, bool b) {
             options.verifyDiagnostics = b;
           })
      .def("auto_parallelize", [](CompilationOptions &options,
                                  bool b) { options.autoParallelize = b; })
      .def("loop_parallelize", [](CompilationOptions &options,
                                  bool b) { options.loopParallelize = b; })
      .def("dataflow_parallelize", [](CompilationOptions &options, bool b) {
        options.dataflowParallelize = b;
      });

  pybind11::class_<mlir::concretelang::JitCompilationResult>(
      m, "JitCompilationResult");
  pybind11::class_<mlir::concretelang::JITLambda,
                   std::shared_ptr<mlir::concretelang::JITLambda>>(m,
                                                                   "JITLambda");
  pybind11::class_<JITLambdaSupport_C>(m, "JITLambdaSupport")
      .def(pybind11::init([](std::string runtimeLibPath) {
        return jit_lambda_support(runtimeLibPath);
      }))
      .def("compile",
           [](JITLambdaSupport_C &support, std::string mlir_program,
              CompilationOptions options) {
             return jit_compile(support, mlir_program.c_str(), options);
           })
      .def("load_client_parameters",
           [](JITLambdaSupport_C &support,
              mlir::concretelang::JitCompilationResult &result) {
             return jit_load_client_parameters(support, result);
           })
      .def(
          "load_server_lambda",
          [](JITLambdaSupport_C &support,
             mlir::concretelang::JitCompilationResult &result) {
            return jit_load_server_lambda(support, result);
          },
          pybind11::return_value_policy::reference)
      .def("server_call",
           [](JITLambdaSupport_C &support, concretelang::JITLambda &lambda,
              clientlib::PublicArguments &publicArguments) {
             return jit_server_call(support, lambda, publicArguments);
           });

  pybind11::class_<mlir::concretelang::LibraryCompilationResult>(
      m, "LibraryCompilationResult")
      .def(pybind11::init([](std::string libraryPath, std::string funcname) {
        return mlir::concretelang::LibraryCompilationResult{
            libraryPath,
            funcname,
        };
      }));
  pybind11::class_<concretelang::serverlib::ServerLambda>(m, "LibraryLambda");
  pybind11::class_<LibraryLambdaSupport_C>(m, "LibraryLambdaSupport")
      .def(pybind11::init([](std::string outputPath) {
        return library_lambda_support(outputPath.c_str());
      }))
      .def("compile",
           [](LibraryLambdaSupport_C &support, std::string mlir_program,
              mlir::concretelang::CompilationOptions options) {
             return library_compile(support, mlir_program.c_str(), options);
           })
      .def("load_client_parameters",
           [](LibraryLambdaSupport_C &support,
              mlir::concretelang::LibraryCompilationResult &result) {
             return library_load_client_parameters(support, result);
           })
      .def(
          "load_server_lambda",
          [](LibraryLambdaSupport_C &support,
             mlir::concretelang::LibraryCompilationResult &result) {
            return library_load_server_lambda(support, result);
          },
          pybind11::return_value_policy::reference)
      .def("server_call",
           [](LibraryLambdaSupport_C &support, serverlib::ServerLambda lambda,
              clientlib::PublicArguments &publicArguments) {
             return library_server_call(support, lambda, publicArguments);
           });

  class ClientSupport {};
  pybind11::class_<ClientSupport>(m, "ClientSupport")
      .def(pybind11::init())
      .def_static(
          "key_set",
          [](clientlib::ClientParameters clientParameters,
             clientlib::KeySetCache *cache) {
            auto optCache =
                cache == nullptr
                    ? llvm::None
                    : llvm::Optional<clientlib::KeySetCache>(*cache);
            return key_set(clientParameters, optCache);
          },
          pybind11::arg().none(false), pybind11::arg().none(true))
      .def_static("encrypt_arguments",
                  [](clientlib::ClientParameters clientParameters,
                     clientlib::KeySet &keySet,
                     std::vector<lambdaArgument> args) {
                    std::vector<mlir::concretelang::LambdaArgument *> argsRef;
                    for (auto i = 0u; i < args.size(); i++) {
                      argsRef.push_back(args[i].ptr.get());
                    }
                    return encrypt_arguments(clientParameters, keySet, argsRef);
                  })
      .def_static("decrypt_result", [](clientlib::KeySet &keySet,
                                       clientlib::PublicResult &publicResult) {
        return decrypt_result(keySet, publicResult);
      });
  pybind11::class_<clientlib::KeySetCache>(m, "KeySetCache")
      .def(pybind11::init<std::string &>());

  pybind11::class_<mlir::concretelang::ClientParameters>(m, "ClientParameters");

  pybind11::class_<clientlib::KeySet>(m, "KeySet");
  pybind11::class_<clientlib::PublicArguments>(m, "PublicArguments");
  pybind11::class_<clientlib::PublicResult>(m, "PublicResult");

  pybind11::class_<lambdaArgument>(m, "LambdaArgument")
      .def_static("from_tensor",
                  [](std::vector<uint8_t> tensor, std::vector<int64_t> dims) {
                    return lambdaArgumentFromTensorU8(tensor, dims);
                  })
      .def_static("from_tensor",
                  [](std::vector<uint16_t> tensor, std::vector<int64_t> dims) {
                    return lambdaArgumentFromTensorU16(tensor, dims);
                  })
      .def_static("from_tensor",
                  [](std::vector<uint32_t> tensor, std::vector<int64_t> dims) {
                    return lambdaArgumentFromTensorU32(tensor, dims);
                  })
      .def_static("from_tensor",
                  [](std::vector<uint64_t> tensor, std::vector<int64_t> dims) {
                    return lambdaArgumentFromTensorU64(tensor, dims);
                  })
      .def_static("from_scalar", lambdaArgumentFromScalar)
      .def("is_tensor",
           [](lambdaArgument &lambda_arg) {
             return lambdaArgumentIsTensor(lambda_arg);
           })
      .def("get_tensor_data",
           [](lambdaArgument &lambda_arg) {
             return lambdaArgumentGetTensorData(lambda_arg);
           })
      .def("get_tensor_shape",
           [](lambdaArgument &lambda_arg) {
             return lambdaArgumentGetTensorDimensions(lambda_arg);
           })
      .def("is_scalar",
           [](lambdaArgument &lambda_arg) {
             return lambdaArgumentIsScalar(lambda_arg);
           })
      .def("get_scalar", [](lambdaArgument &lambda_arg) {
        return lambdaArgumentGetScalar(lambda_arg);
      });
}
