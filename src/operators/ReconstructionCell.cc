/*
  Operators

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <algorithm>
#include <cmath>
#include <vector>

#include "Epetra_Vector.h"

#include "DenseMatrix.hh"
#include "lapack.hh"
#include "Mesh.hh"
#include "OperatorDefs.hh"
#include "Point.hh"
#include "ReconstructionCell.hh"

namespace Amanzi {
namespace Operators {

/* ******************************************************************
* Initialization of basic parameters.
* NOTE: we assume that ghost values of field were already populated.
****************************************************************** */
void ReconstructionCell::Init(Teuchos::RCP<const Epetra_MultiVector> field, 
                              Teuchos::ParameterList& plist,
                              int component)
{
  field_ = field;
  component_ = component;

  ncells_owned = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::Parallel_type::OWNED);
  nfaces_owned = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::Parallel_type::OWNED);
  nnodes_owned = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::Parallel_type::OWNED);

  ncells_wghost = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::Parallel_type::ALL);
  nfaces_wghost = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::Parallel_type::ALL);
  nnodes_wghost = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::Parallel_type::ALL);

  cell_max_nodes = mesh_->cell_get_max_nodes();

  dim = mesh_->space_dimension();

  CompositeVectorSpace cv_space;
  cv_space.SetMesh(mesh_);
  cv_space.SetGhosted(true);
  cv_space.SetComponent("cell", AmanziMesh::CELL, dim);

  gradient_ = Teuchos::RCP<CompositeVector>(new CompositeVector(cv_space, true));

  // process parameters for limiters
  bc_scaling_ = 0.0;

  std::string name = plist.get<std::string>("limiter", "Barth-Jespersen");
  limiter_id_ = 0;
  if (name == "Barth-Jespersen") {
    limiter_id_ = OPERATOR_LIMITER_BARTH_JESPERSEN;
  } else if (name == "tensorial") {
    limiter_id_ = OPERATOR_LIMITER_TENSORIAL;
  } else if (name == "Kuzmin") {
    limiter_id_ = OPERATOR_LIMITER_KUZMIN;
  }

  poly_order_ = plist.get<int>("polynomial order", 0);
  limiter_correction_ = plist.get<bool>("limiter extension for transport", false);
}


/* ******************************************************************
* Gradient of linear reconstruction is based on stabilized 
* least-square fit.
****************************************************************** */
void ReconstructionCell::ComputeGradient()
{
  Teuchos::RCP<Epetra_MultiVector> grad = gradient_->ViewComponent("cell", false);
  AmanziMesh::Entity_ID_List cells;
  AmanziGeometry::Point xcc(dim);

  WhetStone::DenseMatrix matrix(dim, dim);
  WhetStone::DenseVector rhs(dim);

  for (int c = 0; c < ncells_owned; c++) {
    const AmanziGeometry::Point& xc = mesh_->cell_centroid(c);

    // mesh_->cell_get_face_adj_cells(c, AmanziMesh::Parallel_type::ALL, &cells);
    CellFaceAdjCellsNonManifold_(c, AmanziMesh::Parallel_type::ALL, cells);
    int ncells = cells.size();

    matrix.PutScalar(0.0);
    rhs.PutScalar(0.0);

    for (int n = 0; n < ncells; n++) {
      const AmanziGeometry::Point& xc2 = mesh_->cell_centroid(cells[n]);
      for (int i = 0; i < dim; i++) xcc[i] = xc2[i] - xc[i];

      double value = (*field_)[component_][cells[n]] - (*field_)[component_][c];
      PopulateLeastSquareSystem_(xcc, value, matrix, rhs);
    }

    // improve robustness w.r.t degenerate matrices
    double det = matrix.Det();
    double norm = matrix.NormInf();

    if (det < pow(norm, 1.0/dim)) {
      norm *= OPERATOR_RECONSTRUCTION_MATRIX_CORRECTION;
      for (int i = 0; i < dim; i++) matrix(i, i) += norm;
    }

    int info, nrhs = 1;
    WhetStone::DPOSV_F77("U", &dim, &nrhs, matrix.Values(), &dim, rhs.Values(), &dim, &info);
    if (info) {  // reduce reconstruction order
      for (int i = 0; i < dim; i++) rhs(i) = 0.0;
    }

    // rhs[0] = rhs[1] = rhs[2] = 0.0;  // TESTING COMPATABILITY
    for (int i = 0; i < dim; i++) (*grad)[i][c] = rhs(i);
  }

  gradient_->ScatterMasterToGhosted("cell");
}


/* ******************************************************************
* Special implementation of ComputeGradient(). The gradient is computed 
* only in specied cells and internal structures are not modified.
****************************************************************** */
void ReconstructionCell::ComputeGradient(
    const AmanziMesh::Entity_ID_List& ids,
    std::vector<AmanziGeometry::Point>& gradient)
{
  AmanziMesh::Entity_ID_List cells;
  AmanziGeometry::Point xcc(dim), grad(dim);

  WhetStone::DenseMatrix matrix(dim, dim);
  WhetStone::DenseVector rhs(dim);

  gradient.clear();
  for (auto it = ids.begin(); it != ids.end(); ++it) {
    int c = *it;
    const AmanziGeometry::Point& xc = mesh_->cell_centroid(c);

    mesh_->cell_get_face_adj_cells(c, AmanziMesh::Parallel_type::ALL, &cells);
    int ncells = cells.size();

    matrix.PutScalar(0.0);
    rhs.PutScalar(0.0);

    for (int n = 0; n < ncells; n++) {
      const AmanziGeometry::Point& xc2 = mesh_->cell_centroid(cells[n]);
      for (int i = 0; i < dim; i++) xcc[i] = xc2[i] - xc[i];

      double value = (*field_)[component_][cells[n]] - (*field_)[component_][c];
      PopulateLeastSquareSystem_(xcc, value, matrix, rhs);
    }

    // improve robustness w.r.t degenerate matrices
    double det = matrix.Det();
    double norm = matrix.NormInf();

    if (det < pow(norm, 1.0/dim)) {
      norm *= OPERATOR_RECONSTRUCTION_MATRIX_CORRECTION;
      for (int i = 0; i < dim; i++) matrix(i, i) += norm;
    }

    int info, nrhs = 1;
    WhetStone::DPOSV_F77("U", &dim, &nrhs, matrix.Values(), &dim, rhs.Values(), &dim, &info);
    if (info) {  // reduce reconstruction order
      for (int i = 0; i < dim; i++) rhs(i) = 0.0;
    }

    for (int i = 0; i < dim; i++) grad[i] = rhs(i);
    gradient.push_back(grad);
  }
}


/* ******************************************************************
* Apply internal limiter.
****************************************************************** */
void ReconstructionCell::ApplyLimiter(
    const std::vector<int>& bc_model, const std::vector<double>& bc_value)
{
  limiter_ = Teuchos::rcp(new Epetra_Vector(mesh_->cell_map(true)));
  if (limiter_id_ == OPERATOR_LIMITER_BARTH_JESPERSEN) {
    LimiterBarthJespersen_(bc_model, bc_value, limiter_);
    ApplyLimiter(limiter_);
  } else if (limiter_id_ == OPERATOR_LIMITER_TENSORIAL) {
    LimiterTensorial_(bc_model, bc_value);
  } else if (limiter_id_ == OPERATOR_LIMITER_KUZMIN) {
    LimiterKuzmin_(bc_model, bc_value);
  }
}


/* ******************************************************************
* Apply internal limiter over set of cells.
****************************************************************** */
void ReconstructionCell::ApplyLimiter(AmanziMesh::Entity_ID_List& ids,
                                      std::vector<AmanziGeometry::Point>& gradient)
{
  if (limiter_id_ == OPERATOR_LIMITER_KUZMIN) {
    LimiterKuzminSet_(ids, gradient);   
  } else {
    AMANZI_ASSERT(0);
  }
}


/* ******************************************************************
* The limiter must be between 0 and 1
****************************************************************** */
void ReconstructionCell::ApplyLimiter(Teuchos::RCP<Epetra_MultiVector> limiter)
{
  Teuchos::RCP<Epetra_MultiVector> grad = gradient_->ViewComponent("cell", false);

  for (int c = 0; c < ncells_owned; c++) {
    for (int i = 0; i < dim; i++) (*grad)[i][c] *= (*limiter)[0][c];
  }

  gradient_->ScatterMasterToGhosted("cell");
}


/* ******************************************************************
* Calculates reconstructed value at point p.
****************************************************************** */
double ReconstructionCell::getValue(int cell, const AmanziGeometry::Point& p)
{
  Teuchos::RCP<Epetra_MultiVector> grad = gradient_->ViewComponent("cell", false);
  const AmanziGeometry::Point& xc = mesh_->cell_centroid(cell);

  double value = (*field_)[component_][cell];
  for (int i = 0; i < dim; i++) value += (*grad)[i][cell] * (p[i] - xc[i]);

  return value;
}


/* ******************************************************************
* Calculates reconstructed value at point p using external gradient.
****************************************************************** */
double ReconstructionCell::getValue(
    AmanziGeometry::Point& gradient, int cell, const AmanziGeometry::Point& p)
{
  const AmanziGeometry::Point& xc = mesh_->cell_centroid(cell);

  double value = (*field_)[component_][cell];
  for (int i = 0; i < dim; i++) value += gradient[i] * (p[i] - xc[i]);

  return value;
}


/* ******************************************************************
* Assemble a SPD least square matrix
****************************************************************** */
void ReconstructionCell::PopulateLeastSquareSystem_(
    AmanziGeometry::Point& centroid, double field_value,
    WhetStone::DenseMatrix& matrix, WhetStone::DenseVector& rhs)
{
  for (int i = 0; i < dim; i++) {
    double xyz = centroid[i];

    matrix(i, i) += xyz * xyz;
    for (int j = i + 1; j < dim; j++) matrix(j, i) = matrix(i, j) += xyz * centroid[j];

    rhs(i) += xyz * field_value;
  }
}


/* ******************************************************************
* On intersecting manifolds, we extract neighboors living in the same 
* manifold using a smoothness criterion.
****************************************************************** */
void ReconstructionCell::CellFaceAdjCellsNonManifold_(
    AmanziMesh::Entity_ID c, AmanziMesh::Parallel_type ptype,
    std::vector<AmanziMesh::Entity_ID>& cells) const
{
  AmanziMesh::Entity_ID_List faces, fcells;

  mesh_->cell_get_faces(c, &faces);
  int nfaces = faces.size();

  cells.clear();

  for (int n = 0; n < nfaces; ++n) {
    AmanziMesh::Entity_ID f = faces[n];
    mesh_->face_get_cells(f, ptype, &fcells);
    int ncells = fcells.size();

    if (ncells == 2) {
      cells.push_back(fcells[0] + fcells[1] - c);
    } else if (ncells > 2) {
      int dir;
      double dmax(0.0);
      AmanziMesh::Entity_ID cmax;
      const AmanziGeometry::Point& normal0 = mesh_->face_normal(f, false, c, &dir);

      for (int i = 0; i < ncells; ++i) {
        AmanziMesh::Entity_ID c1 = fcells[i];
        if (c1 != c) {
          const AmanziGeometry::Point& normal1 = mesh_->face_normal(f, false, c1, &dir);
          double d = fabs(normal0 * normal1) / norm(normal1);
          if (d > dmax) {
            dmax = d;
            cmax = c1; 
          }
        }
      } 

      cells.push_back(cmax);
    }
  }
}

}  // namespace Operator
}  // namespace Amanzi

