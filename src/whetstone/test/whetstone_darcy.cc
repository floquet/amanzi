/*
The transport component of the Amanzi code, serial unit tests.
License: BSD
Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <vector>

#include "UnitTest++.h"

#include "Teuchos_ParameterList.hpp"
#include "Teuchos_RCP.hpp"
#include "Teuchos_XMLParameterListHelpers.hpp"
#include "Teuchos_SerialDenseMatrix.hpp"
#include "Teuchos_LAPACK.hpp"

#include "MeshFactory.hh"
#include "MeshAudit.hh"

#include "Mesh.hh"
#include "Point.hh"

#include "mfd3d.hh"
#include "tensor.hh"


/* **************************************************************** */
TEST(DARCY_MASS) {
  using namespace Teuchos;
  using namespace Amanzi::AmanziMesh;
  using namespace Amanzi::WhetStone;

  std::cout << "Test: Mass matrix for Darcy" << endl;
#ifdef HAVE_MPI
  Epetra_MpiComm *comm = new Epetra_MpiComm(MPI_COMM_WORLD);
#else
  Epetra_SerialComm *comm = new Epetra_SerialComm();
#endif

  int num_components = 3;

  FrameworkPreference pref;
  pref.clear();
  pref.push_back(Simple);

  MeshFactory meshfactory(comm);
  meshfactory.preference(pref);
  RCP<Mesh> mesh = meshfactory(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1, 1, 1); 
 
  MFD3D mfd(mesh);

  int nfaces = 6, cell = 0;
  Tensor T(3, 1);
  T(0, 0) = 1;

  Teuchos::SerialDenseMatrix<int, double> M(nfaces, nfaces);
  for (int method = 0; method < 1; method++) {
    mfd.DarcyMass(cell, T, M);

    printf("Mass matrix for cell %3d\n", cell);
    for (int i=0; i<nfaces; i++) {
      for (int j=0; j<nfaces; j++ ) printf("%8.4f ", M(i, j)); 
      printf("\n");
    }

    // verify SPD propery
    for (int i=0; i<nfaces; i++) CHECK(M(i, i) > 0.0);

    // verify exact integration property
    Entity_ID_List faces;
    std::vector<int> dirs;
    mesh->cell_get_faces_and_dirs(cell, &faces, &dirs);
    
    double xi, yi, xj;
    double vxx = 0.0, vxy = 0.0, volume = mesh->cell_volume(cell); 
    for (int i = 0; i < nfaces; i++) {
      int f = faces[i];
      xi = mesh->face_normal(f)[0] * dirs[i];
      yi = mesh->face_normal(f)[1] * dirs[i];
      for (int j = 0; j < nfaces; j++) {
        f = faces[j];
        xj = mesh->face_normal(f)[0] * dirs[j];
        vxx += M(i, j) * xi * xj;
        vxy += M(i, j) * yi * xj;
      }
    }
    CHECK_CLOSE(vxx, volume, 1e-10);
    CHECK_CLOSE(vxy, 0.0, 1e-10);
  }

  delete comm;
}


/* **************************************************************** */
TEST(DARCY_INVERSE_MASS) {
  using namespace Teuchos;
  using namespace Amanzi::AmanziMesh;
  using namespace Amanzi::WhetStone;

  std::cout << "\nTest: Inverse mass matrix for Darcy" << endl;
#ifdef HAVE_MPI
  Epetra_MpiComm *comm = new Epetra_MpiComm(MPI_COMM_WORLD);
#else
  Epetra_SerialComm *comm = new Epetra_SerialComm();
#endif

  int num_components = 3;
  FrameworkPreference pref;
  pref.clear();
  pref.push_back(Simple);

  MeshFactory factory(comm);
  factory.preference(pref);
  RCP<Mesh> mesh = factory(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1, 2, 3); 
 
  MFD3D mfd(mesh);

  int nfaces = 6, cell = 0;
  Tensor T(3, 1);  // tensor of rank 1
  T(0, 0) = 1;

  Teuchos::SerialDenseMatrix<int, double> W(nfaces, nfaces);
  for (int method = 0; method < 3; method++) {
    if (method == 0) 
      mfd.DarcyMassInverse(cell, T, W);
    else if (method == 1)
      mfd.DarcyMassInverseScaled(cell, T, W);
    else if (method == 2)
      mfd.DarcyMassInverseOptimizedScaled(cell, T, W);

    printf("Inverse of mass matrix for method=%d\n", method);
    for (int i=0; i<6; i++) {
      for (int j=0; j<6; j++ ) printf("%8.4f ", W(i, j)); 
      printf("\n");
    }

    // verify SPD propery
    for (int i=0; i<nfaces; i++) CHECK(W(i, i) > 0.0);

    // verify exact integration property
    Teuchos::LAPACK<int, double> lapack;
    int info, ipiv[nfaces];
    double work[nfaces];

    lapack.GETRF(nfaces, nfaces, W.values(), nfaces, ipiv, &info);
    lapack.GETRI(nfaces, W.values(), nfaces, ipiv, work, nfaces, &info);

    Entity_ID_List faces;
    std::vector<int> dirs;
    mesh->cell_get_faces_and_dirs(cell, &faces, &dirs);
    
    double xi, yi, xj;
    double vxx = 0.0, vxy = 0.0, volume = mesh->cell_volume(cell); 
    for (int i = 0; i < nfaces; i++) {
      int f = faces[i];
      xi = mesh->face_normal(f)[0] * dirs[i];
      yi = mesh->face_normal(f)[1] * dirs[i];
      for (int j = 0; j < nfaces; j++) {
        f = faces[j];
        xj = mesh->face_normal(f)[0] * dirs[j];
        vxx += W(i, j) * xi * xj;
        vxy += W(i, j) * yi * xj;
      }
    }
    CHECK_CLOSE(vxx, volume, 1e-10);
    CHECK_CLOSE(vxy, 0.0, 1e-10);
  }

  delete comm;
}


