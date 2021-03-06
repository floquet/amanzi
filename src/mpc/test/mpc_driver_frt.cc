#include <iostream>
#include "stdlib.h"
#include "math.h"
#include "UnitTest++.h"

#include <Epetra_Comm.h>
#include <Epetra_MpiComm.h>
#include "Epetra_SerialComm.h"

#include "Teuchos_ParameterList.hpp"
#include "Teuchos_ParameterXMLFileReader.hpp"

#include "CycleDriver.hh"
#include "eos_registration.hh"
#include "Mesh.hh"
#include "MeshFactory.hh"
#include "mpc_pks_registration.hh"
#include "PK_Factory.hh"
#include "PK.hh"
#include "pks_flow_registration.hh"
#include "pks_transport_registration.hh"
#include "pks_chemistry_registration.hh"
#include "State.hh"


TEST(MPC_DRIVER_FLOW_REACTIVE_TRANSPORT) {

using namespace Amanzi;
using namespace Amanzi::AmanziMesh;
using namespace Amanzi::AmanziGeometry;

  Epetra_MpiComm comm = Epetra_MpiComm(MPI_COMM_WORLD);
  
  // read the main parameter list
  std::string xmlInFileName = "test/mpc_driver_frt.xml";
  Teuchos::ParameterXMLFileReader xmlreader(xmlInFileName);
  Teuchos::ParameterList plist = xmlreader.getParameters();
  
  // For now create one geometric model from all the regions in the spec
  Teuchos::ParameterList region_list = plist.get<Teuchos::ParameterList>("regions");
  Teuchos::RCP<Amanzi::AmanziGeometry::GeometricModel> gm =
      Teuchos::rcp(new Amanzi::AmanziGeometry::GeometricModel(3, region_list, &comm));
   
  // create mesh
  FrameworkPreference pref;
  pref.clear();
  pref.push_back(MSTK);
  pref.push_back(STKMESH);

  MeshFactory meshfactory(&comm);
  meshfactory.preference(pref);
  Teuchos::RCP<Mesh> mesh = meshfactory(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 100, 1, 1, gm);
  AMANZI_ASSERT(!mesh.is_null());

  // create dummy observation data object
  double avg1, avg2;
  Amanzi::ObservationData obs_data;    
  Teuchos::RCP<Teuchos::ParameterList> glist = Teuchos::rcp(new Teuchos::ParameterList(plist));

  {
    Amanzi::CycleDriver cycle_driver(glist, mesh, &comm, obs_data);
    try {
      auto S = cycle_driver.Go();
      S->GetFieldData("pressure")->MeanValue(&avg1);
    } catch (...) {
      CHECK(false);
    }

    // check observations
    std::vector<std::string> labels = obs_data.observationLabels();
    std::vector<ObservationData::DataQuadruple> tmp = obs_data[labels[0]];
    for (int k = 1; k < tmp.size(); ++k) {
      CHECK_CLOSE(tmp[k].value, -0.0006, 1.0e-5);
    }
    tmp = obs_data[labels[1]];
    for (int k = 1; k < tmp.size(); ++k) {
      CHECK_CLOSE(tmp[k].value, -0.0002, 1.0e-5);
    }
  }

  // restart simulation and compare results
  glist->sublist("cycle driver").sublist("restart").set<std::string>("file name", "chk_frt00005.h5");

  {
    Amanzi::CycleDriver cycle_driver(glist, mesh, &comm, obs_data);
    try {
      auto S = cycle_driver.Go();
      S->GetFieldData("pressure")->MeanValue(&avg2);
    } catch (...) {
      CHECK(false);
    }
  }

  CHECK_CLOSE(avg1, avg2, 1e-5 * avg1);
}

