/*
  This is the flow test of the Amanzi code. 

  Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "Teuchos_RCP.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_ParameterXMLFileReader.hpp"
#include "UnitTest++.h"

#include "GMVMesh.hh"
#include "MeshFactory.hh"
#include "Richards_PK.hh"
#include "State.hh"

#include "Richards_SteadyState.hh"

/* **************************************************************** */
TEST(FLOW_2D_RICHARDS_SEEPAGE_TPFA) {
  using namespace Teuchos;
  using namespace Amanzi;
  using namespace Amanzi::AmanziMesh;
  using namespace Amanzi::AmanziGeometry;
  using namespace Amanzi::Flow;

  Epetra_MpiComm comm(MPI_COMM_WORLD);
  int MyPID = comm.MyPID();

  if (MyPID == 0) std::cout << "Test: 2D Richards(FV), seepage boundary condition" << std::endl;

  /* read parameter list */
  std::string xmlFileName = "test/flow_richards_seepage_tpfa.xml";
  ParameterXMLFileReader xmlreader(xmlFileName);
  ParameterList plist = xmlreader.getParameters();

  // create a mesh framework
  ParameterList region_list = plist.get<Teuchos::ParameterList>("Regions");
  GeometricModelPtr gm = new GeometricModel(2, region_list, &comm);

  FrameworkPreference pref;
  pref.clear();
  pref.push_back(MSTK);
  pref.push_back(STKMESH);

  MeshFactory meshfactory(&comm);
  meshfactory.preference(pref);
  RCP<const Mesh> mesh = meshfactory(0.0, 0.0, 100.0, 50.0, 100, 50, gm); 

  /* create a simple state and populate it */
  Amanzi::VerboseObject::hide_line_prefix = true;

  Teuchos::ParameterList state_list = plist.get<Teuchos::ParameterList>("State");
  RCP<State> S = rcp(new State(state_list));
  S->RegisterDomainMesh(rcp_const_cast<Mesh>(mesh));

  Teuchos::RCP<Teuchos::ParameterList> global_list(&plist, Teuchos::RCP_WEAK_NO_DEALLOC);
  Richards_PK* RPK = new Richards_PK(global_list, "Flow", S);

  RPK->Setup();
  S->Setup();
  S->InitializeFields();
  S->InitializeEvaluators();

  /* modify the default state for the problem at hand */
  std::string passwd("flow"); 
  Epetra_MultiVector& K = *S->GetFieldData("permeability", passwd)->ViewComponent("cell");
  
  for (int c = 0; c != K.MyLength(); ++c) {
    K[0][c] = 5.0e-13;
    K[1][c] = 5.0e-14;
  }

  double rho = *S->GetScalarData("fluid_density", passwd) = 998.0;
  *S->GetScalarData("fluid_viscosity", passwd) = 0.00089;
  Epetra_Vector& gravity = *S->GetConstantVectorData("gravity", "state");
  double g = gravity[1] = -9.81;

  /* create the initial pressure function */
  Epetra_MultiVector& p = *S->GetFieldData("pressure", passwd)->ViewComponent("cell");
  //Epetra_MultiVector& lambda = *S->GetFieldData("pressure", passwd)->ViewComponent("face");

  double p0(101325.0), z0(30.0);
  for (int c = 0; c < p.MyLength(); c++) {
    const Point& xc = mesh->cell_centroid(c);
    p[0][c] = p0 + rho * g * (xc[1] - z0);
  }
  //RPK->DeriveFaceValuesFromCellValues(p, lambda); 

  /* create Richards process kernel */
  RPK->Initialize();
  S->CheckAllFieldsInitialized();
  RPK->InitTimeInterval();

  /* solve the steady-state problem */
  TI_Specs ti_specs;
  ti_specs.T0 = 0.0;
  ti_specs.dT0 = 1.0;
  ti_specs.T1 = 1e+10;
  ti_specs.max_itrs = 100;

  AdvanceToSteadyState(*RPK, ti_specs, S->GetFieldData("pressure", "flow"));
  RPK->CommitState(0.0, S.ptr());

  const Epetra_MultiVector& ws = *S->GetFieldData("saturation_liquid")->ViewComponent("cell");
  if (MyPID == 0) {
    GMV::open_data_file(*mesh, (std::string)"flow.gmv");
    GMV::start_data();
    GMV::write_cell_data(p, 0, "pressure");
    GMV::write_cell_data(ws, 0, "saturation");
    GMV::close_data_file();
  }

  delete RPK;
}
