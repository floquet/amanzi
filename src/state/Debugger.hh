/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------

License: see $AMANZI_DIR/COPYRIGHT
Author: Ethan Coon

Debugging object for writing debug cells using VerboseObject.

------------------------------------------------------------------------- */

#ifndef AMANZI_DEBUGGER_HH_
#define AMANZI_DEBUGGER_HH_

#include <string>

#include "Teuchos_Ptr.hpp"
#include "Teuchos_ParameterList.hpp"

#include "VerboseObject.hh"
#include "Mesh.hh"

namespace Amanzi {

class CompositeVector;

class Debugger {

 public:
  // Constructor
  Debugger(const Teuchos::RCP<const AmanziMesh::Mesh>& mesh,
           std::string name,
           Teuchos::ParameterList& plist,
           Teuchos::EVerbosityLevel verb_level=Teuchos::VERB_HIGH
           );

  // Write cell + face info
  void WriteCellInfo(bool include_faces=false);

  // Write a vector individually.
  void WriteVector(std::string name,
                   const Teuchos::Ptr<const CompositeVector>& vec,
                   bool include_faces=false);

  // Write list of vectors.
  void WriteVectors(std::vector<std::string> names,
                    std::vector< Teuchos::Ptr<const CompositeVector> >& vecs,
                    bool include_faces=false);

  // call MPI_Comm_Barrier to sync between writing steps
  void Barrier();

  // write a line of ----
  void WriteDivider();

  void SetPrecision(int prec) { precision_ = prec; }
  void SetWidth(int width) { width_ = width; }

  // reverse order -- instead of passing in vector, do writing externally
  Teuchos::RCP<VerboseObject> GetVerboseObject(AmanziMesh::Entity_ID, int rank);

 protected:
  std::string Format_(double dat);
  std::string FormatHeader_(std::string name, int c);

 protected:
  Teuchos::EVerbosityLevel verb_level_;
  Teuchos::RCP<VerboseObject> vo_;
  Teuchos::RCP<const AmanziMesh::Mesh> mesh_;
  std::vector<AmanziMesh::Entity_ID> dc_;
  std::vector<AmanziMesh::Entity_ID> dc_gid_;
  std::vector<Teuchos::RCP<VerboseObject> > dcvo_;

  int width_;
  int header_width_;
  int precision_;
  int cellnum_width_;
  int decimal_width_;
};

} // namespace Amanzi


#endif
