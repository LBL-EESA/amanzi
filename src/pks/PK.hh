/* 
  Amanzi

  Licenses: see $ATS_DIR/COPYRIGHT, $ASCEM_DIR/COPYRIGHT
  Author: Ethan Coon

  Virtual interface for Process Kernels. 
*/

#ifndef ARCOS_PK_HH_
#define ARCOS_PK_HH_

#include "Teuchos_RCP.hpp"

#include "State.hh"

namespace Amanzi {

class PK {
 public:
  // Virtual destructor
  virtual ~PK() {};

  // Setup
  virtual void Setup(const Teuchos::Ptr<State>& S) = 0;

  // Initialize owned (dependent) variables.
  virtual void Initialize(const Teuchos::Ptr<State>& S) = 0;

  // Choose a time step compatible with physics.
  virtual double get_dt() = 0;

  // Advance from state S0 to state S1 at time S0.time + dt.
  // Due to Flow PK / MPC conflict (FIXME when MPC will be upgraded)
  //  virtual int Advance(double dt, double& dt_actual) = 0;
  virtual bool Advance(double dt) = 0;

  // Commit any secondary (dependent) variables.
  virtual void CommitState(double dt, const Teuchos::Ptr<State>& S) = 0;

  // Calculate any diagnostics prior to doing vis
  virtual void CalculateDiagnostics(const Teuchos::Ptr<State>& S) = 0;

  virtual void SetState(const Teuchos::RCP<State>& S) = 0;

  virtual std::string name() = 0;
};

}  // namespace Amanzi

#endif
