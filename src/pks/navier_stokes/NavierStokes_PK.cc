/*
  Navier Stokes PK

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <vector>

// Amanzi::NavierStokes
#include "NavierStokes_PK.hh"

namespace Amanzi {
namespace NavierStokes {

/* ******************************************************************
* New constructor: extracts lists and requires fields.
****************************************************************** */
NavierStokes_PK::NavierStokes_PK(Teuchos::ParameterList& pk_tree,
                                 const Teuchos::RCP<Teuchos::ParameterList>& glist,
                                 const Teuchos::RCP<State>& S,
                                 const Teuchos::RCP<TreeVector>& soln) :
    soln_(soln),
    passwd_("navier stokes")
{
  S_ = S;

  std::string pk_name = pk_tree.name();
  const char* result = pk_name.data();

  boost::iterator_range<std::string::iterator> res = boost::algorithm::find_last(pk_name,"->"); 
  if (res.end() - pk_name.end() != 0) boost::algorithm::erase_head(pk_name,  res.end() - pk_name.begin());

  // We need the flow list
  Teuchos::RCP<Teuchos::ParameterList> pk_list = Teuchos::sublist(glist, "PKs", true);
  ns_list_ = Teuchos::sublist(pk_list, pk_name, true);

  // We also need iscaleneous sublists
  preconditioner_list_ = Teuchos::sublist(glist, "preconditioners", true);
  linear_solver_list_ = Teuchos::sublist(glist, "solvers", true);
  ti_list_ = Teuchos::sublist(ns_list_, "time integrator", true);
}


/* ******************************************************************
* Old constructor for unit tests.
****************************************************************** */
NavierStokes_PK::NavierStokes_PK(const Teuchos::RCP<Teuchos::ParameterList>& glist,
                                 const std::string& pk_list_name,
                                 Teuchos::RCP<State> S,
                                 const Teuchos::RCP<TreeVector>& soln) :
    glist_(glist),
    soln_(soln),
    passwd_("navier stokes")
{
  S_ = S;

  // We need the flow list
  Teuchos::RCP<Teuchos::ParameterList> pk_list = Teuchos::sublist(glist, "PKs", true);
  ns_list_ = Teuchos::sublist(pk_list, "Navier Stokes", true);
 
  // We also need miscaleneous sublists
  preconditioner_list_ = Teuchos::sublist(glist, "preconditioners", true);
  linear_solver_list_ = Teuchos::sublist(glist, "solvers", true);
  ti_list_ = Teuchos::sublist(ns_list_, "time integrator");

  vo_ = Teuchos::null;
}


/* ******************************************************************
* Define structure of this PK. We request physical fields and their
* evaluators. Selection of a few models is available and driven by
* model factories, evaluator factories, and parameters of the list
* "physical models and assumptions".
****************************************************************** */
void NavierStokes_PK::Setup(const Teuchos::Ptr<State>& S)
{
  dt_ = 0.0;
  mesh_ = S->GetMesh();
  dim = mesh_->space_dimension();

  // primary fields
  // -- pressure
  if (!S->HasField("pressure")) {
    S->RequireField("pressure", passwd_)->SetMesh(mesh_)->SetGhosted(true)
      ->SetComponent("cell", AmanziMesh::CELL, 1);

    Teuchos::ParameterList elist;
    elist.set<std::string>("evaluator name", "pressure");
    pressure_eval_ = Teuchos::rcp(new PrimaryVariableFieldEvaluator(elist));
    S->SetFieldEvaluator("pressure", pressure_eval_);
  }

  // -- velocity
  std::vector<std::string> names = {"node", "face"};
  std::vector<AmanziMesh::Entity_kind> locations = {AmanziMesh::NODE, AmanziMesh::FACE};
  std::vector<int> ndofs = {dim, 1};

  if (!S->HasField("fluid_velocity")) {
    S->RequireField("fluid_velocity", passwd_)->SetMesh(mesh_)->SetGhosted(true)
      ->SetComponents(names, locations, ndofs);

    Teuchos::ParameterList elist;
    elist.set<std::string>("evaluator name", "fluid_velocity");
    fluid_velocity_eval_ = Teuchos::rcp(new PrimaryVariableFieldEvaluator(elist));
    S->SetFieldEvaluator("fluid_velocity", fluid_velocity_eval_);
  }

  // -- viscosity: if not requested by any PK, we request its constant value.
  if (!S->HasField("fluid_viscosity")) {
    if (!S->HasField("fluid_viscosity")) {
      S->RequireScalar("fluid_viscosity", passwd_);
    }
  }
}


/* ******************************************************************
* This is a long but simple routine. It goes through flow parameter
* list and initializes various objects including those created during 
* the setup step.
****************************************************************** */
void NavierStokes_PK::Initialize(const Teuchos::Ptr<State>& S)
{
  // Initialize miscalleneous defaults.
  // -- times
  double t_ini = S->time(); 
  dt_desirable_ = dt_;
  dt_next_ = dt_;

  // -- mesh dimensions
  ncells_owned = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  ncells_wghost = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::USED);

  nfaces_owned = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  nfaces_wghost = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::USED);

  nnodes_owned = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::OWNED);
  nnodes_wghost = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::USED);

  // Create verbosity object to print out initialiation statistics.
  Teuchos::ParameterList vlist;
  vlist.sublist("verbose object") = ns_list_->sublist("verbose object");
  vo_ = Teuchos::rcp(new VerboseObject("NavierStokes", vlist)); 

  if (vo_->getVerbLevel() >= Teuchos::VERB_MEDIUM) {
    Teuchos::OSTab tab = vo_->getOSTab();
    *vo_->os()<< "\nPK initialization started...\n";
  }

  // Create pointers to the primary flow field pressure.
  Teuchos::RCP<TreeVector> tmp_u = Teuchos::rcp(new TreeVector());
  Teuchos::RCP<TreeVector> tmp_p = Teuchos::rcp(new TreeVector());
  soln_->PushBack(tmp_u);
  soln_->PushBack(tmp_p);
 
  soln_p_ = S->GetFieldData("pressure", passwd_);
  soln_u_ = S->GetFieldData("fluid_velocity", passwd_);
  soln_->SubVector(0)->SetData(soln_u_); 
  soln_->SubVector(1)->SetData(soln_p_); 

  // Initialize time integrator.
  std::string ti_method_name = ti_list_->get<std::string>("time integration method", "none");
  ASSERT(ti_method_name == "BDF1");
  Teuchos::ParameterList& bdf1_list = ti_list_->sublist("BDF1");

  if (! bdf1_list.isSublist("verbose object"))
      bdf1_list.sublist("verbose object") = ns_list_->sublist("verbose object");

  bdf1_dae_ = Teuchos::rcp(new BDF1_TI<TreeVector, TreeVectorSpace>(*this, bdf1_list, soln_));

  // Create BC objects.
  bcf_model_.resize(nfaces_wghost, Operators::OPERATOR_BC_NONE);
  bcf_value_.resize(nfaces_wghost, 0.0);
  bcf_mixed_.resize(nfaces_wghost, 0.0);
  bcf_ = Teuchos::rcp(new Operators::BCs(Operators::OPERATOR_BC_TYPE_FACE, bcf_model_, bcf_value_, bcf_mixed_));

  bcv_model_.resize(nnodes_wghost, Operators::OPERATOR_BC_NONE);
  bcv_value_.resize(nnodes_wghost);
  bcv_ = Teuchos::rcp(new Operators::BCs(Operators::OPERATOR_BC_TYPE_NODE, bcv_model_, bcv_value_));

  // Initialize matrix and preconditioner operators.
  // -- create elastic block
  Teuchos::ParameterList& tmp1 = ns_list_->sublist("operators")
                                          .sublist("elasticity operator");
  op_matrix_elas_ = Teuchos::rcp(new Operators::Elasticity(tmp1, mesh_));
  op_preconditioner_elas_ = Teuchos::rcp(new Operators::Elasticity(tmp1, mesh_));

  // -- create divergence block
  Teuchos::ParameterList& tmp2 = ns_list_->sublist("operators")
                                          .sublist("divergence operator");
  op_div_ = Teuchos::rcp(new Operators::AdvectionRiemann(tmp2, mesh_));

  // -- create pressure block 
  op_mass_ = Teuchos::rcp(new Operators::Accumulation(AmanziMesh::CELL, mesh_));

  // -- matrix and preconditioner
  op_matrix_ = Teuchos::rcp(new Operators::TreeOperator(Teuchos::rcpFromRef(soln_->Map())));
  op_matrix_->SetOperatorBlock(0, 0, op_matrix_elas_->global_operator());
  op_matrix_->SetOperatorBlock(1, 0, op_div_->global_operator());
  op_matrix_->SetOperatorBlock(0, 1, op_div_->global_operator(), true);

  op_preconditioner_ = Teuchos::rcp(new Operators::TreeOperator(Teuchos::rcpFromRef(soln_->Map())));
  op_preconditioner_->SetOperatorBlock(0, 0, op_preconditioner_elas_->global_operator());
  op_preconditioner_->SetOperatorBlock(1, 1, op_mass_->global_operator());

  // -- setup phase
  double mu = *S_->GetScalarData("fluid_viscosity", passwd_);
  op_matrix_elas_->global_operator()->Init();
  op_matrix_elas_->SetTensorCoefficient(mu);
  op_matrix_elas_->SetBCs(bcf_, bcf_);
  op_matrix_elas_->AddBCs(bcv_, bcv_);

  op_preconditioner_elas_->global_operator()->Init();
  op_preconditioner_elas_->SetTensorCoefficient(mu);
  op_preconditioner_elas_->SetBCs(bcf_, bcf_);
  op_preconditioner_elas_->AddBCs(bcv_, bcv_);

  // -- assemble phase
  op_matrix_elas_->UpdateMatrices();
  op_matrix_elas_->ApplyBCs(true, true);

  op_preconditioner_elas_->UpdateMatrices();
  op_preconditioner_elas_->ApplyBCs(true, true);
  op_preconditioner_elas_->global_operator()->SymbolicAssembleMatrix();

  CompositeVector vol(op_mass_->global_operator()->DomainMap());
  vol.PutScalar(1.0);
  op_mass_->AddAccumulationTerm(vol, 1.0, "cell");
  op_mass_->global_operator()->SymbolicAssembleMatrix();

  // -- generic linear solver for maost cases
  ASSERT(ti_list_->isParameter("linear solver"));
  solver_name_ = ti_list_->get<std::string>("linear solver");

  // -- preconditioner or encapsulated preconditioner
  ASSERT(ti_list_->isParameter("preconditioner"));
  preconditioner_name_ = ti_list_->get<std::string>("preconditioner");
  ASSERT(preconditioner_list_->isSublist(preconditioner_name_));
  
  op_pc_solver_ = op_preconditioner_;
}


/* ******************************************************************* 
* Performs one time step from time t_old to time t_new either for
* steady-state or transient simulation.
******************************************************************* */
bool NavierStokes_PK::AdvanceStep(double t_old, double t_new, bool reinit)
{
  dt_ = t_new - t_old;

  // save a copy of primary and conservative fields
  CompositeVector pressure_copy(*S_->GetFieldData("pressure", passwd_));
  CompositeVector fluid_velocity_copy(*S_->GetFieldData("fluid_velocity", passwd_));

  // initialization
  if (num_itrs_ == 0) {
    Teuchos::RCP<TreeVector> udot = Teuchos::rcp(new TreeVector(*soln_));
    udot->PutScalar(0.0);
    bdf1_dae_->SetInitialState(t_old, soln_, udot);

    UpdatePreconditioner(t_old, soln_, dt_);
    num_itrs_++;
  }

  // trying to make a step
  bool failed(false);
  failed = bdf1_dae_->TimeStep(dt_, dt_next_, soln_);
  if (failed) {
    dt_ = dt_next_;

    // revover the original primary solution
    *S_->GetFieldData("pressure", passwd_) = pressure_copy;
    pressure_eval_->SetFieldAsChanged(S_.ptr());

    *S_->GetFieldData("fluid_velocity", passwd_) = fluid_velocity_copy;
    fluid_velocity_eval_->SetFieldAsChanged(S_.ptr());

    Teuchos::OSTab tab = vo_->getOSTab();
    *vo_->os() << "Reverted pressure, fluid_velocity" << std::endl;

    return failed;
  }

  // commit solution (should we do it here ?)
  bdf1_dae_->CommitSolution(dt_, soln_);
  pressure_eval_->SetFieldAsChanged(S_.ptr());
  fluid_velocity_eval_->SetFieldAsChanged(S_.ptr());

  num_itrs_++;
  dt_ = dt_next_;
  
  return failed;
}
 
}  // namespace NavierStokes
}  // namespace Amanzi
