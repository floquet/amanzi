/*
  WhetStone, version 2.1
  Release name: naka-to.

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)

  Mimetic schemes for generalized polyhedra.
*/

#ifndef AMANZI_MFD3D_GENERALIZED_DIFFUSION_HH_
#define AMANZI_MFD3D_GENERALIZED_DIFFUSION_HH_

#include "Teuchos_RCP.hpp"

#include "Mesh.hh"

#include "DenseMatrix.hh"
#include "MFD3D.hh"
#include "Tensor.hh"

namespace Amanzi {
namespace WhetStone {

class MFD3D_Generalized_Diffusion : public MFD3D { 
 public:
  MFD3D_Generalized_Diffusion(const Teuchos::RCP<const AmanziMesh::Mesh>& mesh) 
    : MFD3D(mesh), 
      InnerProduct(mesh) {};
  ~MFD3D_Generalized_Diffusion() {};

  // required member functions
  // -- mass matrices
  virtual int L2consistency(int c, const Tensor& T, DenseMatrix& N, DenseMatrix& Mc, bool symmetry);
  virtual int MassMatrix(int c, const Tensor& T, DenseMatrix& M); 

  virtual int L2consistencyInverse(int c, const Tensor& T, DenseMatrix& R, DenseMatrix& Wc, bool symmetry);
  virtual int MassMatrixInverse(int c, const Tensor& T, DenseMatrix& W); 

  // -- stiffness matrices
  virtual int H1consistency(int c, const Tensor& K, DenseMatrix& N, DenseMatrix& Ac) { return 0; }
  virtual int StiffnessMatrix(int c, const Tensor& K, DenseMatrix& A);

  // -- adevction matrices
  virtual int DivergenceMatrix(int c, DenseMatrix& A);

 private:
  void CurvedFaceGeometry_(int f, int dirs,
                           std::vector<AmanziGeometry::Point>& vv, 
                           std::vector<AmanziGeometry::Point>& xm);
};

}  // namespace WhetStone
}  // namespace Amanzi

#endif

