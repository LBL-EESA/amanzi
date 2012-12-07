/*
This is the flow component of the Amanzi code.
Frequently used bundles of routines are wrapped into computational blocks. 

Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
Amanzi is released under the three-clause BSD License. 
The terms of use and "as is" disclaimer for this license are 
provided Reconstruction.cppin the top-level COPYRIGHT file.

Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include "Matrix_MFD.hpp"
#include "Flow_State.hpp"
#include "Richards_PK.hpp"

namespace Amanzi {
namespace AmanziFlow {

/* ******************************************************************
* A wrapper for updating relative permeabilities.
****************************************************************** */
void Richards_PK::CalculateRelativePermeability(const Epetra_Vector& u)
{
  Epetra_Vector* u_cells = FS->CreateCellView(u);
  Epetra_Vector* u_faces = FS->CreateFaceView(u);

  if (Krel_method == FLOW_RELATIVE_PERM_UPWIND_GRAVITY ||
      Krel_method == FLOW_RELATIVE_PERM_UPWIND_DARCY_FLUX ||
      Krel_method == FLOW_RELATIVE_PERM_ARITHMETIC_MEAN) {
    CalculateRelativePermeabilityFace(*u_cells);
    Krel_cells->PutScalar(1.0);
  } else if (Krel_method == FLOW_RELATIVE_PERM_EXPERIMENTAL) {
    CalculateRelativePermeabilityFace(*u_cells);
  } else {
    CalculateRelativePermeabilityCell(*u_cells);
    Krel_faces->PutScalar(1.0);
  }
}

 
/* ******************************************************************
* A wrapper for updating boundary conditions.
****************************************************************** */
void Richards_PK::UpdateBoundaryConditions(double Tp, Epetra_Vector& p_faces)
{
  bc_pressure->Compute(Tp);
  bc_flux->Compute(Tp);
  bc_head->Compute(Tp);
  bc_seepage->Compute(Tp);
  ProcessBoundaryConditions(
      bc_pressure, bc_head, bc_flux, bc_seepage, rainfall_factor,
      p_faces, atm_pressure,
      bc_markers, bc_values);

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_EXTREME) {
     std::printf("Richards PK: updating boundary conditions at T(sec)=%14.9e\n", Tp);
  }
}


/* ******************************************************************
* A wrapper for generating a steady state problem. 
* Warning: Krel must be initialized before calling this routine. 
****************************************************************** */
void Richards_PK::AssembleSteadyStateProblem_MFD(Matrix_MFD* matrix_operator, bool add_preconditioner)
{ 
  matrix_operator->CreateMFDstiffnessMatrices(*Krel_cells, *Krel_faces, Krel_method);
  matrix_operator->CreateMFDrhsVectors();
  AddGravityFluxes_MFD(K, *Krel_cells, *Krel_faces, Krel_method, matrix_operator);
  matrix_operator->ApplyBoundaryConditions(bc_markers, bc_values);
  matrix_operator->AssembleGlobalMatrices();

  if (add_preconditioner) {
    matrix_operator->ComputeSchurComplement(bc_markers, bc_values);
  }

  // DEBUG
  // Matrix_Audit audit(mesh_, matrix);
  // audit.InitAudit();
  // audit.CheckSpectralBounds();
}


/* ******************************************************************
* A wrapper for generating a transient problem. 
* Warning: Krel must be initialized before calling this routine. 
****************************************************************** */
void Richards_PK::AssembleTransientProblem_MFD(Matrix_MFD* matrix_operator, double dTp,
                                               Epetra_Vector& p, bool add_preconditioner)
{ 
  matrix_operator->CreateMFDstiffnessMatrices(*Krel_cells, *Krel_faces, Krel_method);
  matrix_operator->CreateMFDrhsVectors();
  AddGravityFluxes_MFD(K, *Krel_cells, *Krel_faces, Krel_method, matrix_operator);
  AddTimeDerivative_MFD(p, dTp, matrix_operator);
  matrix_operator->ApplyBoundaryConditions(bc_markers, bc_values);
  matrix_operator->AssembleGlobalMatrices();

  if (add_preconditioner) {
    matrix_operator->ComputeSchurComplement(bc_markers, bc_values);
  }
}

}  // namespace AmanziFlow
}  // namespace Amanzi



