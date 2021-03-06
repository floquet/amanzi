/* -*-  mode: c++; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------

ATS

License: see $ATS_DIR/COPYRIGHT
Author: Ethan Coon

Factory for a CompositeVector on an Amanzi mesh.

This should be thought of as a vector-space: it lays out data components as a
mesh along with entities on the mesh.  This meta-data can be used with the
mesh's *_map() methods to create the data.

This class is very light weight as it maintains only meta-data.

------------------------------------------------------------------------- */

#ifndef AMANZI_COMPOSITEVECTOR_SPACE_HH_
#define AMANZI_COMPOSITEVECTOR_SPACE_HH_

#include <vector>
#include "Teuchos_RCP.hpp"

#include "dbc.hh"
#include "Mesh.hh"

namespace Amanzi {

class CompositeVector;

class CompositeVectorSpace {

public:
  // Constructor
  CompositeVectorSpace();
  CompositeVectorSpace(const CompositeVectorSpace& other);
  CompositeVectorSpace(const CompositeVectorSpace& other, bool ghosted);

  // assignment
  CompositeVectorSpace& operator=(const CompositeVectorSpace&) = default;
  
  // CompositeVectorSpace is a factory of CompositeVectors
  Teuchos::RCP<CompositeVector> Create() const;
  
  // Checks equality
  bool SameAs(const CompositeVectorSpace& other) const;
  bool SubsetOf(const CompositeVectorSpace& other) const;

  // -------------------------------------
  // Specs for the construction of CVs
  // -------------------------------------

  // CompositeVectors exist on a single communicator.
  const Epetra_MpiComm& Comm() const { return *mesh_->get_comm(); }

  // mesh specification
  Teuchos::RCP<const AmanziMesh::Mesh> Mesh() const { return mesh_; }
  CompositeVectorSpace* SetMesh(const Teuchos::RCP<const AmanziMesh::Mesh>& mesh);

  // ghost spec
  bool Ghosted() const { return ghosted_; }
  CompositeVectorSpace* SetGhosted(bool ghosted=true);

  // Is this space and the resulting CV owned by a PK?
  bool Owned() const { return owned_; }
  void SetOwned(bool owned=true) { owned_ = owned; }

  // Components are refered to by names.
  // -- Iteration over names of the space
  typedef std::vector<std::string>::const_iterator name_iterator;
  name_iterator begin() const { return names_.begin(); }
  name_iterator end() const { return names_.end(); }
  unsigned int size() const { return names_.size(); }

  bool HasComponent(std::string name) const {
    return indexmap_.find(name) != indexmap_.end(); }
  int NumComponents() const { return size(); }

  // Each component has a number of Degrees of Freedom.
  int NumVectors(std::string name) const {
    return num_dofs_[Index_(name)]; }

  // Each component exists on a mesh entity kind. (CELL, FACE, NODE, etc)
  AmanziMesh::Entity_kind Location(std::string name) const {
    return locations_[Index_(name)]; }

  // Update all specs from another space's specs.
  // Useful for PKs to maintain default factories that apply to multiple CVs.
  CompositeVectorSpace* Update(const CompositeVectorSpace& other);

  // component specification

  // Add methods append their specs to the space's spec, checking to make
  // sure the spec is OK if the full spec has been set (by an owning PK).
  CompositeVectorSpace*
  AddComponent(std::string name,
               AmanziMesh::Entity_kind location,
               int num_dofs);

  CompositeVectorSpace*
  AddComponents(const std::vector<std::string>& names,
                const std::vector<AmanziMesh::Entity_kind>& locations,
                const std::vector<int>& num_dofs);

  // Set methods fix the component specs, checking to make sure all previously
  // added specs are contained in the new spec.
  CompositeVectorSpace*
  SetComponent(std::string name,
               AmanziMesh::Entity_kind location,
               int num_dofs);

  CompositeVectorSpace*
  SetComponents(const std::vector<std::string>& names,
                const std::vector<AmanziMesh::Entity_kind>& locations,
                const std::vector<int>& num_dofs);

private:
  // Indexing of name->int
  int Index_(std::string name) const {
    std::map<std::string, int>::const_iterator item = indexmap_.find(name);
    AMANZI_ASSERT(item != indexmap_.end());
    return item->second;
  }

  void InitIndexMap_();

  // Consistency checking.
  bool CheckContained_(const std::vector<std::string>& containing,
                       const std::vector<std::string>& contained);

  bool CheckConsistent_(const std::vector<std::string>& names1,
                        const std::vector<AmanziMesh::Entity_kind>& locations1,
                        const std::vector<int>& num_dofs1,
                        const std::vector<std::string>& names2,
                        const std::vector<AmanziMesh::Entity_kind>& locations2,
                        const std::vector<int>& num_dofs2);

  bool UnionAndConsistent_(const std::vector<std::string>& names1,
                           const std::vector<AmanziMesh::Entity_kind>& locations1,
                           const std::vector<int>& num_dofs1,
                           std::vector<std::string>& names2,
                           std::vector<AmanziMesh::Entity_kind>& locations2,
                           std::vector<int>& num_dofs2);

private:
  bool ghosted_;

  // data
  bool owned_;
  Teuchos::RCP<const AmanziMesh::Mesh> mesh_;
  std::vector<std::string> names_;
  std::map< std::string, int > indexmap_;

  std::vector<AmanziMesh::Entity_kind> locations_;
  std::vector<int> num_dofs_;

  friend class CompositeVector;
};

} // namespace amanzi

#endif
