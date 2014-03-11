/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------

License: see $AMANZI_DIR/COPYRIGHT
Author (v1): Neil Carlson
       (v2): Ethan Coon

Function applied to a mesh component with at most one function application 
per entity.

------------------------------------------------------------------------- */
#ifndef AMANZI_FLOW_BOUNDARY_FUNCTION_HH_
#define AMANZI_FLOW_BOUNDARY_FUNCTION_HH_

#include <vector>
#include <map>
#include <string>

#include "Teuchos_RCP.hpp"

#include "Mesh.hh"
#include "MultiFunction.hh"
#include "unique_mesh_function.hh"

namespace Amanzi {
namespace Functions {

const int BOUNDARY_FUNCTION_ACTION_NONE = 0;
const int BOUNDARY_FUNCTION_ACTION_HEAD_RELATIVE = 1;

typedef std::pair<std::string, int> Action;

class FlowBoundaryFunction : public UniqueMeshFunction {
 public:
  FlowBoundaryFunction(const Teuchos::RCP<const AmanziMesh::Mesh> &mesh) :
      UniqueMeshFunction(mesh),
      finalized_(false) {}
  
  void Define(const std::vector<std::string> &regions,
              const Teuchos::RCP<const MultiFunction> &f, 
              int method);
  void Define(std::string region,
              const Teuchos::RCP<const MultiFunction> &f,
              int method);

  void Compute(double time);
  void ComputeShift(double T, double* shift);

  void Finalize();

  // access / set
  const std::vector<Action>& actions() { return actions_; } 
  inline double reference_pressure() { return reference_pressure_; }
  inline double set_reference_pressure(double p0) { reference_pressure_ = p0; }

  // iterator methods
  typedef std::map<int,double>::const_iterator Iterator;
  Iterator begin() const { return value_.begin(); }
  Iterator end() const  { return value_.end(); }
  Iterator find(const int j) const { return value_.find(j); }
  std::map<int,double>::size_type size() { return value_.size(); }

 protected:
  std::map<int,double> value_;
  bool finalized_;

 private:
  std::vector<Action> actions_;
  double reference_pressure_;
};

} // namespace
} // namespace


#endif