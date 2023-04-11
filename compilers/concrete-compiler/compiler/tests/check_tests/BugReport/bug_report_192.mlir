// RUN: concretecompiler --action=dump-tfhe --force-encoding crt %s
func.func @main(%arg0: tensor<3x2x!FHE.esint<16>>) -> tensor<3x2x!FHE.esint<16>> {
  %c3_i17 = arith.constant 3 : i17
  %0 = "FHELinalg.apply_lookup_table"(%arg0, %cst) : (tensor<3x2x!FHE.esint<16>>, tensor<65536xi64>) -> tensor<3x2x!FHE.esint<16>>
  return %0 : tensor<3x2x!FHE.esint<16>>
}