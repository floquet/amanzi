/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
// -------------------------------------------------------------
/**
 * @file   FrameworkTraits.hh
 * @author William A. Perkins
 * @date Mon Aug  1 13:47:02 2011
 * 
 * @brief  
 * 
 * 
 */
// -------------------------------------------------------------
// -------------------------------------------------------------
// Created March 10, 2011 by William A. Perkins
// Last Change: Mon Aug  1 13:47:02 2011 by William A. Perkins <d3g096@PE10900.pnl.gov>
// -------------------------------------------------------------
#ifndef _FrameworkTraits_hh_
#define _FrameworkTraits_hh_

#include <Epetra_MpiComm.h>
#include <Teuchos_RCP.hpp>
#include <Teuchos_ParameterList.hpp>

#include "MeshFramework.hh"
#include "MeshFileType.hh"
#include "Mesh.hh"

#include "GeometricModel.hh"

#include "errors.hh"
#include "exceptions.hh"

namespace Amanzi {
namespace AmanziMesh {

/// Is the specified framework available
extern bool framework_available(const Framework& f);

/// General routine to see if a format can be read by particular framework
extern bool framework_reads(const Framework& f, const Format& fmt, const bool& parallel);

/// Read a mesh
extern Teuchos::RCP<Mesh> 
framework_read(const Epetra_MpiComm *comm, const Framework& f, const std::string& fname,
               const AmanziGeometry::GeometricModelPtr& gm);

/// General routine to see if a mesh can be generated by a particular framework
extern bool framework_generates(const Framework& f, const bool& parallel, const unsigned int& dimension);

/// Generate a hexahedral mesh
extern Teuchos::RCP<Mesh> 
framework_generate(const Epetra_MpiComm *comm, const Framework& f, 
                   const double& x0, const double& y0, const double& z0,
                   const double& x1, const double& y1, const double& z1,
                   const unsigned int& nx, const unsigned int& ny, const unsigned int& nz,
                   const AmanziGeometry::GeometricModelPtr& gm);

/// Generate a quadrilateral mesh
extern Teuchos::RCP<Mesh> 
framework_generate(const Epetra_MpiComm *comm, const Framework& f, 
                   const double& x0, const double& y0,
                   const double& x1, const double& y1,
                   const unsigned int& nx, const unsigned int& ny,
                   const AmanziGeometry::GeometricModelPtr& gm);

extern Teuchos::RCP<Mesh> 
framework_generate(const Epetra_MpiComm *comm, const Framework& f, 
                   Teuchos::ParameterList &parameter_list,
                   const AmanziGeometry::GeometricModelPtr& gm);

} // namespace AmanziMesh
} // namespace Amanzi

#endif
