/*
This is the transport component of the Amanzi code. 

Copyright 2010-2013 held jointly by LANS/LANL, LBNL, and PNNL. 
Amanzi is released under the three-clause BSD License. 
The terms of use and "as is" disclaimer for this license are 
provided in the top-level COPYRIGHT file.

Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <algorithm>
#include <vector>

#include "Epetra_Vector.h"
#include "Epetra_IntVector.h"
#include "Epetra_MultiVector.h"
#include "Epetra_Import.h"
#include "Teuchos_RCP.hpp"

#include "Mesh.hh"
#include "errors.hh"
#include "tabular-function.hh"
#include "GMVMesh.hh"

#include "Explicit_TI_RK.hh"

#include "Transport_PK.hh"
#include "Reconstruction.hh"

namespace Amanzi {
namespace AmanziTransport {

/* ******************************************************************
* We set up minimum default values and call Init() routine to 
* complete initialization.
****************************************************************** */
Transport_PK::Transport_PK(Teuchos::ParameterList &parameter_list_MPC,
                           Teuchos::RCP<Transport_State> TS_MPC)
{
  status = TRANSPORT_NULL;

  parameter_list = parameter_list_MPC;
  number_components = TS_MPC->total_component_concentration()->NumVectors();

  TS = Teuchos::rcp(new Transport_State(*TS_MPC, Transport_State::CONSTRUCT_MODE_COPY_POINTERS));

  dT = dT_debug = T_physics = 0.0;
  double time = TS->initial_time();
  if (time >= 0.0) T_physics = time;

  verbosity = TRANSPORT_VERBOSITY_HIGH;
  internal_tests = 0;
  dispersivity_model = TRANSPORT_DISPERSIVITY_MODEL_NULL;
  tests_tolerance = TRANSPORT_CONCENTRATION_OVERSHOOT;

  MyPID = 0;
  mesh_ = TS->mesh();
  dim = mesh_->space_dimension();

  flow_mode = TRANSPORT_FLOW_TRANSIENT;
  bc_scaling = 0.0;
  mass_tracer_exact = 0.0;
}


/* ******************************************************************
* Routine processes parameter list. It needs to be called only once
* on each processor.                                                     
****************************************************************** */
int Transport_PK::InitPK()
{
  TS_nextBIG = Teuchos::rcp(new Transport_State(*TS, Transport_State::CONSTRUCT_MODE_COPY_DATA_GHOSTED));
  TS_nextMPC = Teuchos::rcp(new Transport_State(*TS_nextBIG, Transport_State::CONSTRUCT_MODE_VIEW_DATA));

  ncells_owned = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  ncells_wghost = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::USED);

  nfaces_owned = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  nfaces_wghost = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::USED);

  nnodes_wghost = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::USED);

#ifdef HAVE_MPI
  const Epetra_Map& source_cmap = mesh_->cell_map(false);
  const Epetra_Map& target_cmap = mesh_->cell_map(true);

  cell_importer = Teuchos::rcp(new Epetra_Import(target_cmap, source_cmap));

  const Epetra_Map& source_fmap = mesh_->face_map(false);
  const Epetra_Map& target_fmap = mesh_->face_map(true);

  face_importer = Teuchos::rcp(new Epetra_Import(target_fmap, source_fmap));

  const Epetra_Comm& comm = source_cmap.Comm();
  MyPID = comm.MyPID();
#endif

  ProcessParameterList();

  const Epetra_Map& fmap = mesh_->face_map(true);
  upwind_cell_ = Teuchos::rcp(new Epetra_IntVector(fmap));  // The maps include both owned and ghosts
  downwind_cell_ = Teuchos::rcp(new Epetra_IntVector(fmap));

  // advection block initialization
  current_component_ = -1;  // default value may be useful in the future
  const Epetra_Map& cmap = mesh_->cell_map(true);
  component_ = Teuchos::rcp(new Epetra_Vector(cmap));
  component_next_ = Teuchos::rcp(new Epetra_Vector(cmap));

  component_local_min_.resize(ncells_owned);
  component_local_max_.resize(ncells_owned);

  const Epetra_Map& cmap_false = mesh_->cell_map(false);
  ws_subcycle_start = Teuchos::rcp(new Epetra_Vector(cmap_false));
  ws_subcycle_end = Teuchos::rcp(new Epetra_Vector(cmap_false));

  // advection_limiter = TRANSPORT_LIMITER_TENSORIAL;
  limiter_ = Teuchos::rcp(new Epetra_Vector(cmap));

  lifting.reset_field(mesh_, component_);
  lifting.Init();

  // boundary conditions initialization
  double time = T_physics;
  for (int i = 0; i < bcs.size(); i++) {
    bcs[i]->Compute(time);
  }

  CheckInfluxBC();

  // source term memory allocation (revisit the code (lipnikov@lanl.gov)
  if (src_sink_distribution & Functions::TransportActions::DOMAIN_FUNCTION_ACTION_DISTRIBUTE_PERMEABILITY) {
    Kxy = Teuchos::rcp(new Epetra_Vector(mesh_->cell_map(false)));
  }
 
  return 0;
}


/* *******************************************************************
 * Estimation of the time step based on T.Barth (Lecture Notes   
 * presented at VKI Lecture Series 1994-05, Theorem 4.2.2.       
 * Routine must be called every time we update a flow field.
 *
 * Warning: Barth calculates influx, we calculate outflux. The methods
 * are equivalent for divergence-free flows and gurantee EMP. Outflux 
 * takes into account sinks and sources but preserves only positivity
 * of an advected mass.
 ****************************************************************** */
double Transport_PK::CalculateTransportDt()
{
  // flow could not be available at initialization, copy it again
  if (status == TRANSPORT_NULL) {
    TS->CopyMasterMultiCell2GhostMultiCell(TS->ref_total_component_concentration(),
                                           TS_nextBIG->ref_total_component_concentration());
    TS->CopyMasterFace2GhostFace(TS->ref_darcy_flux(), TS_nextBIG->ref_darcy_flux());

    if (internal_tests) CheckDivergenceProperty();
    IdentifyUpwindCells();

    status = TRANSPORT_FLOW_AVAILABLE;

  } else if (flow_mode == TRANSPORT_FLOW_TRANSIENT) {
    TS->CopyMasterFace2GhostFace(TS->ref_darcy_flux(), TS_nextBIG->ref_darcy_flux());

    IdentifyUpwindCells();

    status = TRANSPORT_FLOW_AVAILABLE;
  }

  // loop over faces and accumulate upwinding fluxes
  int  i, f, c, c1;

  Teuchos::RCP<const AmanziMesh::Mesh> mesh = TS->mesh();
  const Epetra_Map& fmap = mesh->face_map(true);
  const Epetra_Vector& darcy_flux = TS_nextBIG->ref_darcy_flux();

  std::vector<double> total_outflux(ncells_wghost, 0.0);

  for (f = 0; f < nfaces_wghost; f++) {
    c = (*upwind_cell_)[f];
    if (c >= 0) total_outflux[c] += fabs(darcy_flux[f]);
  }

  // loop over cells and calculate minimal dT
  double outflux, dT_cell;
  const Epetra_Vector& ws_prev = TS->ref_prev_water_saturation();
  const Epetra_Vector& ws = TS->ref_water_saturation();
  const Epetra_Vector& phi = TS->ref_porosity();

  dT = dT_cell = TRANSPORT_LARGE_TIME_STEP;
  int cmin_dT = 0;
  for (c = 0; c < ncells_owned; c++) {
    outflux = total_outflux[c];
    if (outflux) dT_cell = mesh->cell_volume(c) * phi[c] * std::min(ws_prev[c], ws[c]) / outflux;
    if (dT_cell < dT) {
      dT = dT_cell;
      cmin_dT = c;
    }
  }
  if (spatial_disc_order == 2) dT /= 2;

  // communicate global time step
  double dT_tmp = dT;
#ifdef HAVE_MPI
  const Epetra_Comm& comm = ws_prev.Comm();
  comm.MinAll(&dT_tmp, &dT, 1);
#endif

  // incorporate developers and CFL constraints
  dT = std::min(dT, dT_debug);
  dT *= cfl_;

  // print optional diagnostics using maximum cell id as the filter
  if (verbosity >= TRANSPORT_VERBOSITY_HIGH) {
    int cmin_dT_unique = (fabs(dT_tmp * cfl_ - dT) < 1e-6 * dT) ? cmin_dT : -1;
 
#ifdef HAVE_MPI
    int cmin_dT_tmp = cmin_dT_unique;
    comm.MaxAll(&cmin_dT_tmp, &cmin_dT_unique, 1);
#endif
    if (cmin_dT == cmin_dT_unique) {
      const AmanziGeometry::Point& p = mesh_->cell_centroid(cmin_dT);
      printf("Transport PK: cell %d has smallest dT, (%9.6f, %9.6f", cmin_dT, p[0], p[1]);
      if (p.dim() == 3) 
        printf(", %9.6f)\n", p[2]);
      else
        printf(")\n"); 
    }
  }
  return dT;
}


/* ******************************************************************* 
* Estimate returns last time step unless it is zero.     
******************************************************************* */
double Transport_PK::EstimateTransportDt()
{
  if (dT == 0.0) CalculateTransportDt();
  return dT;
}


/* ******************************************************************* 
* MPC will call this function to advance the transport state.
* Efficient subcycling requires to calculate an intermediate state of
* saturation only once, which leads to a leap-frog-type algorithm.
* 
* WARNIN: We cannot assume that dT_MPC equals to the global time step.
******************************************************************* */
void Transport_PK::Advance(double dT_MPC)
{
  Epetra_MultiVector& tcc = TS->ref_total_component_concentration();
  Epetra_MultiVector& tcc_next = TS_nextBIG->ref_total_component_concentration();

  const Epetra_Vector& ws_prev = TS->ref_prev_water_saturation();
  const Epetra_Vector& ws = TS->ref_water_saturation();

  // calculate stable time step dT
  double dT_shift = 0.0, dT_global = dT_MPC;
  double time = TS->intermediate_time();
  if (time >= 0.0) { 
    T_physics = time;
    dT_shift = TS->initial_time() - time;
    dT_global = TS->final_time() - TS->initial_time();
  }

  CalculateTransportDt();
  double dT_original = dT;  // advance routines override dT
  int interpolate_ws = (dT < dT_global) ? 1 : 0;

  // start subcycling
  double dT_sum = 0.0;
  if (flow_mode == TRANSPORT_FLOW_TRANSIENT) {
    TS->CopyMasterFace2GhostFace(TS->ref_darcy_flux(), TS_nextBIG->ref_darcy_flux());
  }

  double dT_cycle;
  if (interpolate_ws) {
    dT_cycle = dT_original;
    TS->InterpolateCellVector(ws_prev, ws, dT_shift, dT_global, *ws_subcycle_start);
  } else {
    dT_cycle = dT_MPC;
    water_saturation_start = TS->prev_water_saturation();
    water_saturation_end = TS->water_saturation();
  }

  int ncycles = 0, swap = 1;
  while (dT_sum < dT_MPC) {
    // update boundary conditions
    time = T_physics;
    for (int i = 0; i < bcs.size(); i++) bcs[i]->Compute(time);
    
    double dT_try = dT_MPC - dT_sum;
    bool final_cycle = false;
    if (dT_try >= 2 * dT_original) {
      dT_cycle = dT_original;
    } else if (dT_try > dT_original) { 
      dT_cycle = dT_try / 2; 
    } else {
      dT_cycle = dT_try;
      final_cycle = true;
    }

    T_physics += dT_cycle;
    dT_sum += dT_cycle;

    if (interpolate_ws) {
      if (swap) {  // Initial water saturation is in 'start'.
        water_saturation_start = ws_subcycle_start;
        water_saturation_end = ws_subcycle_end;

        double dT_int = dT_sum + dT_shift;
        TS->InterpolateCellVector(ws_prev, ws, dT_int, dT_global, *ws_subcycle_end);
      } else {  // Initial water saturation is in 'end'.
        water_saturation_start = ws_subcycle_end;
        water_saturation_end = ws_subcycle_start;

        double dT_int = dT_sum + dT_shift;
        TS->InterpolateCellVector(ws_prev, ws, dT_int, dT_global, *ws_subcycle_start);
      }
      swap = 1 - swap;
    }

    if (spatial_disc_order == 1) {  // temporary solution (lipnikov@lanl.gov)
      AdvanceDonorUpwind(dT_cycle);
    } else if (spatial_disc_order == 2 && temporal_disc_order == 1) {
      AdvanceSecondOrderUpwindRK1(dT_cycle);
    } else if (spatial_disc_order == 2 && temporal_disc_order == 2) {
      AdvanceSecondOrderUpwindRK2(dT_cycle);
      // AdvanceSecondOrderUpwindGeneric(dT_cycle);
    }

    if (! final_cycle) TS->CopyMasterMultiCell2GhostMultiCell(tcc_next, tcc, 0);  // rotate concentrations

    ncycles++;
  }

  dT = dT_original;  // restore the original dT (just in case)

  if (MyPID == 0 && verbosity >= TRANSPORT_VERBOSITY_MEDIUM) {
    printf("Transport PK: %d sub-cycles, dT_stable: %10.5g [sec]  dT_MPC: %10.5g [sec]\n", 
        ncycles, dT_original, dT_MPC);
  }

  if (verbosity >= TRANSPORT_VERBOSITY_MEDIUM) {
    double tccmin_vec[number_components];
    double tccmax_vec[number_components];

    TS->MinValueMasterCells(tcc_next, tccmin_vec);
    TS->MaxValueMasterCells(tcc_next, tccmax_vec);

    double tccmin, tccmax;
    tcc_next.Comm().MinAll(tccmin_vec, &tccmin, 1);  // find the global extrema
    tcc_next.Comm().MaxAll(tccmax_vec, &tccmax, 1);  // find the global extrema

    const Epetra_Vector& phi = TS->ref_porosity();
    mass_tracer_exact += TracerVolumeChangePerSecond(0) * dT_MPC;
    double mass_tracer = 0.0;
    for (int c = 0; c < ncells_owned; c++) {
      mass_tracer += ws[c] * phi[c] * tcc_next[0][c] * mesh_->cell_volume(c);
    }

    double mass_tracer_tmp = mass_tracer, mass_exact_tmp = mass_tracer_exact, mass_exact;
    mesh_->get_comm()->SumAll(&mass_tracer_tmp, &mass_tracer, 1);
    mesh_->get_comm()->SumAll(&mass_exact_tmp, &mass_exact, 1);

    if (MyPID == 0) {
      double mass_loss = mass_exact - mass_tracer;
      printf("Transport PK: tracer: %9.6g to %9.6g  at %12.7g [sec]\n", tccmin, tccmax, T_physics);
      printf("        mass: %10.5e [kg], mass left domain: %10.5e [kg]\n", mass_tracer, mass_loss);
    }
  }

  // DEBUG
  // writeGMVfile(TS_nextMPC);
}


/* ******************************************************************* 
 * A simple first-order transport method 
 ****************************************************************** */
void Transport_PK::AdvanceDonorUpwind(double dT_cycle)
{
  status = TRANSPORT_STATE_BEGIN;
  dT = dT_cycle;  // overwrite the maximum stable transport step

  const Epetra_Vector& darcy_flux = TS_nextBIG->ref_darcy_flux();
  const Epetra_Vector& phi = TS_nextBIG->ref_porosity();

  // populating next state of concentrations
  Teuchos::RCP<Epetra_MultiVector> tcc = TS->total_component_concentration();
  Teuchos::RCP<Epetra_MultiVector> tcc_next = TS_nextBIG->total_component_concentration();
  TS_nextBIG->CopyMasterMultiCell2GhostMultiCell(*tcc, *tcc_next);

  // prepare conservative state in master and slave cells
  double vol_phi_ws, tcc_flux;
  int num_components = tcc->NumVectors();

  for (int c = 0; c < ncells_owned; c++) {
    vol_phi_ws = mesh_->cell_volume(c) * phi[c] * (*water_saturation_start)[c];

    for (int i = 0; i < num_components; i++)
      (*tcc_next)[i][c] = (*tcc)[i][c] * vol_phi_ws;
  }

  // advance all components at once
  double u;
  for (int f = 0; f < nfaces_wghost; f++) {  // loop over master and slave faces
    int c1 = (*upwind_cell_)[f];
    int c2 = (*downwind_cell_)[f];

    u = fabs(darcy_flux[f]);

    if (c1 >=0 && c1 < ncells_owned && c2 >= 0 && c2 < ncells_owned) {
      for (int i = 0; i < num_components; i++) {
        tcc_flux = dT * u * (*tcc)[i][c1];
        (*tcc_next)[i][c1] -= tcc_flux;
        (*tcc_next)[i][c2] += tcc_flux;
      }

    } else if (c1 >=0 && c1 < ncells_owned && (c2 >= ncells_owned || c2 < 0)) {
      for (int i = 0; i < num_components; i++) {
        tcc_flux = dT * u * (*tcc)[i][c1];
        (*tcc_next)[i][c1] -= tcc_flux;
      }

    } else if (c1 >= ncells_owned && c2 >= 0 && c2 < ncells_owned) {
      for (int i = 0; i < num_components; i++) {
        tcc_flux = dT * u * (*tcc_next)[i][c1];
        (*tcc_next)[i][c2] += tcc_flux;
      }
    }
  }

  // loop over exterior boundary sets
  for (int n = 0; n < bcs.size(); n++) {
    int i = bcs_tcc_index[n];

    for (Amanzi::Functions::TransportBoundaryFunction::Iterator bc = bcs[n]->begin(); bc != bcs[n]->end(); ++bc) {
      int f = bc->first;
      int c2 = (*downwind_cell_)[f];

      if (c2 >= 0) {
        u = fabs(darcy_flux[f]);
        tcc_flux = dT * u * bc->second;
        (*tcc_next)[i][c2] += tcc_flux;
      }
    }
  }

  // process external sources
  if (src_sink != NULL) {
    double time = T_physics;
    ComputeAddSourceTerms(time, dT, src_sink, *tcc_next);
  }

  // recover concentration from new conservative state
  for (int c = 0; c < ncells_owned; c++) {
    vol_phi_ws = mesh_->cell_volume(c) * phi[c] * (*water_saturation_end)[c];
    for (int i = 0; i < num_components; i++) (*tcc_next)[i][c] /= vol_phi_ws;
  }

  if (internal_tests) {
    Teuchos::RCP<Epetra_MultiVector> tcc_nextMPC = TS_nextMPC->total_component_concentration();
    CheckGEDproperty(*tcc_nextMPC);
  }

  status = TRANSPORT_STATE_COMPLETE;
}


/* ******************************************************************* 
 * We have to advance each component independently due to different
 * reconstructions. We use tcc when only owned data are needed and 
 * tcc_next when owned and ghost data. This is a special routine for 
 * transient flow and uses first-order time integrator. 
 ****************************************************************** */
void Transport_PK::AdvanceSecondOrderUpwindRK1(double dT_cycle)
{
  status = TRANSPORT_STATE_BEGIN;
  dT = dT_cycle;  // overwrite the maximum stable transport step

  Teuchos::RCP<Epetra_MultiVector> tcc = TS->total_component_concentration();
  Teuchos::RCP<Epetra_MultiVector> tcc_next = TS_nextBIG->total_component_concentration();
  Epetra_Vector f_component(*component_);

  int num_components = tcc->NumVectors();
  for (int i = 0; i < num_components; i++) {
    current_component_ = i;  // needed by BJ 

    Epetra_Vector*& tcc_component = (*tcc)(i);
    TS_nextBIG->CopyMasterCell2GhostCell(*tcc_component, *component_);

    double T = 0.0;
    fun(T, *component_, f_component);

    double ws_ratio;
    for (int c = 0; c < ncells_owned; c++) {
      ws_ratio = (*water_saturation_start)[c] / (*water_saturation_end)[c];
      (*tcc_next)[i][c] = ((*tcc)[i][c] + dT * f_component[c]) * ws_ratio;
    }
  }

  if (internal_tests) {
    Teuchos::RCP<Epetra_MultiVector> tcc_nextMPC = TS_nextMPC->total_component_concentration();
    CheckGEDproperty(*tcc_nextMPC);
  }

  status = TRANSPORT_STATE_COMPLETE;
}


/* ******************************************************************* 
 * We have to advance each component independently due to different
 * reconstructions. This is a special routine for transient flow and 
 * uses second-order predictor-corrector time integrator. 
 ****************************************************************** */
void Transport_PK::AdvanceSecondOrderUpwindRK2(double dT_cycle)
{
  status = TRANSPORT_STATE_BEGIN;
  dT = dT_cycle;  // overwrite the maximum stable transport step

  Teuchos::RCP<Epetra_MultiVector> tcc = TS->total_component_concentration();
  Teuchos::RCP<Epetra_MultiVector> tcc_next = TS_nextBIG->total_component_concentration();
  Epetra_Vector f_component(*component_);

  Epetra_Vector ws_ratio(*water_saturation_start);
  for (int c = 0; c < ncells_owned; c++) ws_ratio[c] /= (*water_saturation_end)[c];

  for (int i = 0; i < tcc->NumVectors(); i++) {
    current_component_ = i;  // needed by BJ 

    // predictor step
    Epetra_Vector*& tcc_component = (*tcc)(i);
    TS_nextBIG->CopyMasterCell2GhostCell(*tcc_component, *component_);

    double T = 0.0;
    fun(T, *component_, f_component);

    for (int c = 0; c < ncells_owned; c++) {
      (*tcc_next)[i][c] = ((*tcc)[i][c] + dT * f_component[c]) * ws_ratio[c];
    }

    // corrector step
    Epetra_Vector*& tcc_next_component = (*tcc_next)(i);
    TS_nextBIG->CopyMasterCell2GhostCell(*tcc_next_component, *component_);

    fun(T, *component_, f_component);

    for (int c = 0; c < ncells_owned; c++) {
      double value = ((*tcc)[i][c] + dT * f_component[c]) * ws_ratio[c];
      (*tcc_next)[i][c] = ((*tcc_next)[i][c] + value) / 2;
    }
  }

  if (internal_tests) {
    Teuchos::RCP<Epetra_MultiVector> tcc_nextMPC = TS_nextMPC->total_component_concentration();
    CheckGEDproperty(*tcc_nextMPC);
  }

  status = TRANSPORT_STATE_COMPLETE;
}


/* ******************************************************************* 
 * We have to advance each component independently due to different
 * reconstructions. We use tcc when only owned data are needed and 
 * tcc_next when owned and ghost data.
 *
 * Data flow: loop over components and apply the generic RK2 method.
 * The generic means that saturation is constant during time step. 
 ****************************************************************** */
void Transport_PK::AdvanceSecondOrderUpwindGeneric(double dT_cycle)
{
  status = TRANSPORT_STATE_BEGIN;
  dT = dT_cycle;  // overwrite the maximum stable transport step

  Teuchos::RCP<Epetra_MultiVector> tcc = TS->total_component_concentration();
  Teuchos::RCP<Epetra_MultiVector> tcc_next = TS_nextBIG->total_component_concentration();

  // define time integration method
  Explicit_TI::RK::method_t ti_method = Explicit_TI::RK::forward_euler;
  if (temporal_disc_order == 2) {
    ti_method = Explicit_TI::RK::heun_euler;
  }
  Explicit_TI::RK TVD_RK(*this, ti_method, *component_);

  int num_components = tcc->NumVectors();
  for (int i = 0; i < num_components; i++) {
    current_component_ = i;  // it is needed in BJ called inside RK:fun

    Epetra_Vector*& tcc_component = (*tcc)(i);
    TS_nextBIG->CopyMasterCell2GhostCell(*tcc_component, *component_);

    double T = 0.0;  // requires fixes (lipnikov@lanl.gov)
    TVD_RK.step(T, dT, *component_, *component_next_);

    for (int c = 0; c < ncells_owned; c++) (*tcc_next)[i][c] = (*component_next_)[c];
  }

  if (internal_tests) {
    Teuchos::RCP<Epetra_MultiVector> tcc_nextMPC = TS_nextMPC->total_component_concentration();
    CheckGEDproperty(*tcc_nextMPC);
  }

  status = TRANSPORT_STATE_COMPLETE;
}


/* ******************************************************************
* Computes source and sink terms and adds them to vector tcc.                                   
****************************************************************** */
void Transport_PK::ComputeAddSourceTerms(double Tp, double dTp, 
                                         Functions::TransportDomainFunction* src_sink, 
                                         Epetra_MultiVector& tcc)
{
  int num_components = tcc.NumVectors();
  for (int i = 0; i < num_components; i++) {
    std::string name(TS->get_component_name(i));
    
    if (src_sink_distribution & 
        Functions::TransportActions::DOMAIN_FUNCTION_ACTION_DISTRIBUTE_PERMEABILITY)
      src_sink->ComputeDistributeMultiValue(Tp, name, Kxy->Values()); 
    else
      src_sink->ComputeDistributeMultiValue(Tp, name, NULL);

    Functions::TransportDomainFunction::Iterator src;
    for (src = src_sink->begin(); src != src_sink->end(); ++src) {
      int c = src->first;
      tcc[i][c] += dTp * mesh_->cell_volume(c) * src->second;
    }
  }
}


/* *******************************************************************
 * Identify flux direction based on orientation of the face normal 
 * and sign of the  Darcy velocity.                               
 ****************************************************************** */
void Transport_PK::IdentifyUpwindCells()
{
  Teuchos::RCP<const AmanziMesh::Mesh> mesh = TS->mesh();

  for (int f = 0; f < nfaces_wghost; f++) {
    (*upwind_cell_)[f] = -1;  // negative value indicates boundary
    (*downwind_cell_)[f] = -1;
  }

  AmanziMesh::Entity_ID_List faces;
  std::vector<int> fdirs;
  Epetra_Vector& darcy_flux = TS_nextBIG->ref_darcy_flux();

  for (int c = 0; c < ncells_wghost; c++) {
    mesh->cell_get_faces_and_dirs(c, &faces, &fdirs);

    for (int i = 0; i < faces.size(); i++) {
      int f = faces[i];
      if (darcy_flux[f] * fdirs[i] >= 0) {
        (*upwind_cell_)[f] = c;
      } else {
        (*downwind_cell_)[f] = c;
      }
    }
  }
}








}  // namespace AmanziTransport
}  // namespace Amanzi

