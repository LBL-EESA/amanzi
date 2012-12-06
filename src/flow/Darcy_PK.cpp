/*
This is the flow component of the Amanzi code.

Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
Amanzi is released under the three-clause BSD License. 
The terms of use and "as is" disclaimer for this license are 
provided in the top-level COPYRIGHT file.

Authors: Neil Carlson (version 1) 
         Konstantin Lipnikov (version 2) (lipnikov@lanl.gov)
*/

#include <vector>

#include "Epetra_Vector.h"
#include "Epetra_Import.h"

#include "errors.hh"
#include "exceptions.hh"

#include "mfd3d.hpp"
#include "tensor.hpp"

#include "Flow_State.hpp"
#include "Flow_constants.hpp"
#include "Darcy_PK.hpp"
#include "Matrix_MFD.hpp"
#include "Matrix_MFD_TPFA.hpp"

namespace Amanzi {
namespace AmanziFlow {

/* ******************************************************************
* each variable initialization
****************************************************************** */
Darcy_PK::Darcy_PK(Teuchos::ParameterList& global_list, Teuchos::RCP<Flow_State> FS_MPC)
{
  // initialize pointers (Do we need smart pointers here? lipnikov@lanl.gov)
  bc_pressure = NULL; 
  bc_head = NULL;
  bc_flux = NULL;
  bc_seepage = NULL; 
  src_sink = NULL;

  super_map_ = NULL; 
  solver = NULL; 
  matrix_ = NULL; 
  preconditioner_ = NULL;

  Flow_PK::Init(FS_MPC);  // sets up default parameters
  FS = FS_MPC;

  // extract important sublists
  Teuchos::ParameterList flow_list;
  if (global_list.isSublist("Flow")) {
    flow_list = global_list.sublist("Flow");
  } else {
    Errors::Message msg("Darcy PK: input parameter list does not have <Flow> sublist.");
    Exceptions::amanzi_throw(msg);
  }

  if (flow_list.isSublist("Darcy Problem")) {
    dp_list_ = flow_list.sublist("Darcy Problem");
  } else {
    Errors::Message msg("Darcy PK: input parameter list does not have <Darcy Problem> sublist.");
    Exceptions::amanzi_throw(msg);
  }

  if (global_list.isSublist("Preconditioners")) {
    preconditioner_list_ = global_list.sublist("Preconditioners");
  } else {
    Errors::Message msg("Darcy PK: input parameter list does not have <Preconditioners> sublist.");
    Exceptions::amanzi_throw(msg);
  }

  if (global_list.isSublist("Solvers")) {
    solver_list_ = global_list.sublist("Solvers");
  } else {
    Errors::Message msg("Darcy PK: input parameter list does not have <Solvers> sublist.");
    Exceptions::amanzi_throw(msg);
  }

  mesh_ = FS->mesh();
  dim = mesh_->space_dimension();

  // Create the combined cell/face DoF map.
  super_map_ = CreateSuperMap();

  // Other fundamental physical quantities
  rho_ = *(FS->fluid_density());
  mu_ = *(FS->fluid_viscosity());
  gravity_.init(dim);
  for (int k = 0; k < dim; k++) gravity_[k] = (*(FS->gravity()))[k];

#ifdef HAVE_MPI
  const Epetra_Comm& comm = mesh_->cell_map(false).Comm();
  MyPID = comm.MyPID();

  const Epetra_Map& source_cmap = mesh_->cell_map(false);
  const Epetra_Map& target_cmap = mesh_->cell_map(true);

  cell_importer_ = Teuchos::rcp(new Epetra_Import(target_cmap, source_cmap));

  const Epetra_Map& source_fmap = mesh_->face_map(false);
  const Epetra_Map& target_fmap = mesh_->face_map(true);

  face_importer_ = Teuchos::rcp(new Epetra_Import(target_fmap, source_fmap));
#endif

  // time control
  set_time(0.0, 1e-8);  // default parameters
  dT_desirable_ = dT;

  // miscalleneous
  mfd3d_method = FLOW_MFD3D_OPTIMIZED;  // will be changed (lipnikov@lanl.gov)
  verbosity = FLOW_VERBOSITY_HIGH;
  src_sink_distribution = FLOW_SOURCE_DISTRIBUTION_NONE;
}


/* ******************************************************************
* Clean memory.
****************************************************************** */
Darcy_PK::~Darcy_PK()
{
  delete super_map_;
  delete solver;
  if (matrix_ == preconditioner_) {
    delete matrix_;
  } else {
    delete matrix_;
    delete preconditioner_;
  }
  delete bc_pressure;
  delete bc_head;
  delete bc_flux;
  delete bc_seepage;
}


/* ******************************************************************
* Extract information from Diffusion Problem parameter list.
****************************************************************** */
void Darcy_PK::InitPK()
{
  matrix_ = new Matrix_MFD(FS, *super_map_);
  preconditioner_ = matrix_;

  // Create the solution vectors.
  solution = Teuchos::rcp(new Epetra_Vector(*super_map_));
  solution_cells = Teuchos::rcp(FS->CreateCellView(*solution));
  solution_faces = Teuchos::rcp(FS->CreateFaceView(*solution));

  const Epetra_BlockMap& cmap = mesh_->cell_map(false);
  pdot_cells_prev = Teuchos::rcp(new Epetra_Vector(cmap));
  pdot_cells = Teuchos::rcp(new Epetra_Vector(cmap));
  
  // Create algebraic solver
  solver = new AztecOO;
  solver->SetUserOperator(matrix_);
  solver->SetPrecOperator(preconditioner_);
  solver->SetAztecOption(AZ_solver, AZ_cg);

  // Get parameters from the flow parameter list.
  ProcessParameterList();

  // Process boundary data
  bc_tuple zero = {0.0, 0.0};
  bc_markers.resize(nfaces_wghost, FLOW_BC_FACE_NULL);
  bc_values.resize(nfaces_wghost, zero);

  ProcessShiftWaterTableList();

  double time = FS->get_time();
  if (time >= 0.0) T_physics = time;

  time = T_physics;
  bc_pressure->Compute(time);
  bc_flux->Compute(time);
  bc_seepage->Compute(time);
  if (shift_water_table_.getRawPtr() == NULL) {
    bc_head->Compute(time);
  } else {
    bc_head->ComputeShift(time, shift_water_table_->Values());
  }
  ProcessBoundaryConditions(
      bc_pressure, bc_head, bc_flux, bc_seepage,
      *solution_faces, atm_pressure,
      bc_markers, bc_values);

  // Process other fundamental structures
  K.resize(ncells_owned);
  matrix_->SetSymmetryProperty(true);
  matrix_->SymbolicAssembleGlobalMatrices(*super_map_);

  // Allocate data for relative permeability (for consistency).
  Krel_cells = Teuchos::rcp(new Epetra_Vector(mesh_->cell_map(true)));
  Krel_faces = Teuchos::rcp(new Epetra_Vector(mesh_->face_map(true)));

  Krel_cells->PutScalar(1.0);
  Krel_faces->PutScalar(1.0);  // must go away (lipnikov@lanl.gov)

  // Preconditioner = matrix for saturated flow
  int method = ti_specs_sss.preconditioner_method;
  Teuchos::ParameterList& tmp_list = preconditioner_list_.sublist(ti_specs_sss.preconditioner_name);
  Teuchos::ParameterList ML_list;
  if (method == FLOW_PRECONDITIONER_TRILINOS_ML) {
    ML_list = tmp_list.sublist("ML Parameters"); 
  } else if (method == FLOW_PRECONDITIONER_HYPRE_AMG) {
    ML_list = tmp_list.sublist("BoomerAMG Parameters"); 
  } else if (method == FLOW_PRECONDITIONER_TRILINOS_BLOCK_ILU) {
    ML_list = tmp_list.sublist("Block ILU Parameters");
  }
  preconditioner_->InitPreconditioner(method, ML_list);

  // Allocate memory for wells
  if (src_sink_distribution == FLOW_SOURCE_DISTRIBUTION_PERMEABILITY) {
    Kxy = Teuchos::rcp(new Epetra_Vector(mesh_->cell_map(false)));
  }

  flow_status_ = FLOW_STATUS_INIT;
};


/* ******************************************************************
* Initialization of auxiliary variables (lambda and two saturations).
* WARNING: Flow_PK may use complex initialization of the remaining 
* state variables.
****************************************************************** */
void Darcy_PK::InitializeAuxiliaryData()
{
  // pressures (lambda is not important when solver is very accurate)
  Epetra_Vector& pressure = FS->ref_pressure();
  Epetra_Vector& lambda = FS->ref_lambda();
  DeriveFaceValuesFromCellValues(pressure, lambda);

  // saturations
  Epetra_Vector& ws = FS->ref_water_saturation();
  Epetra_Vector& ws_prev = FS->ref_prev_water_saturation();

  ws_prev.PutScalar(1.0);
  ws.PutScalar(1.0);

  // miscalleneous
  UpdateSpecificYield();
}


/* ******************************************************************
* Wrapper for a steady-state solver
****************************************************************** */
void Darcy_PK::InitializeSteadySaturated()
{ 
  double T = FS->get_time();
  SolveFullySaturatedProblem(T, *solution);
}


/* ******************************************************************
* Separate initialization of solver may be required for steady state
* and transient runs.       
****************************************************************** */
void Darcy_PK::InitSteadyState(double T0, double dT0)
{
  set_time(T0, dT0);
  dT_desirable_ = dT0;  // The minimum desirable time step from now on.

  Epetra_Vector& pressure = FS->ref_pressure();
  *solution_cells = pressure;

  // initialize mass matrices
  SetAbsolutePermeabilityTensor(K);
  for (int c = 0; c < K.size(); c++) K[c] *= rho_ / mu_;
  matrix_->CreateMFDmassMatrices(mfd3d_method, K);

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_HIGH) {
    int nokay = matrix_->nokay();
    int npassed = matrix_->npassed();
    std::printf("Darcy PK: successful and passed matrices: %8d %8d\n", nokay, npassed);   
  }

  // Well modeling
  if (src_sink_distribution == FLOW_SOURCE_DISTRIBUTION_PERMEABILITY) {
    CalculatePermeabilityFactorInWell(K, *Kxy);
  }

  // make initial guess consistent with boundary conditions
  if (ti_specs_sss.initialize_with_darcy) {
    ti_specs_sss.initialize_with_darcy = false;
    DeriveFaceValuesFromCellValues(pressure, *solution_faces);
    SolveFullySaturatedProblem(T0, *solution);
    pressure = *solution_cells;
  }

  flow_status_ = FLOW_STATUS_STEADY_STATE;
}


/* ******************************************************************
* Initialization analyzes status of matrix/preconditioner pair.      
****************************************************************** */
void Darcy_PK::InitTransient(double T0, double dT0)
{
  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_MEDIUM) {
    std::printf("***********************************************************\n");
    std::printf("Darcy PK: initializing transient flow: T(sec)=%10.5e dT(sec)=%9.4e\n", T0, dT0);
    std::printf("          source/sink distribution method (id) %1d\n", src_sink_distribution);
    std::printf("          time stepping strategy is %2d\n", ti_specs_sss.dT_method);
    if (ti_specs_sss.initialize_with_darcy)
        std::printf("          will enforce consistency of initial and boundary data\n");
    std::printf("***********************************************************\n");
  }

  Epetra_Vector& pressure = FS->ref_pressure();
  *solution_cells = pressure;

  set_time(T0, dT0);
  dT_desirable_ = dT0;  // The minimum desirable time step from now on.
  num_itrs_trs = 0;

  // initialize mass matrices
  SetAbsolutePermeabilityTensor(K);
  for (int c = 0; c < K.size(); c++) K[c] *= rho_ / mu_;
  matrix_->CreateMFDmassMatrices(mfd3d_method, K);

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_HIGH) {
    int nokay = matrix_->nokay();
    int npassed = matrix_->npassed();
    std::printf("Darcy PK: successful and passed matrices: %8d %8d\n", nokay, npassed);   
  }

  // Well modeling
  if (src_sink_distribution == FLOW_SOURCE_DISTRIBUTION_PERMEABILITY) {
    CalculatePermeabilityFactorInWell(K, *Kxy);
  }

  // make initial guess consistent with boundary conditions
  if (ti_specs_sss.initialize_with_darcy) {
    ti_specs_sss.initialize_with_darcy = false;
    DeriveFaceValuesFromCellValues(pressure, *solution_faces);
    SolveFullySaturatedProblem(T0, *solution);
    pressure = *solution_cells;
  }

  flow_status_ = FLOW_STATUS_TRANSIENT_STATE;

  // DEBUG
  // SolveFullySaturatedProblem(0.0, *solution);
  // CommitState(FS); WriteGMVfile(FS); exit(0);
}


/* ******************************************************************
* Wrapper for a steady-state solver
****************************************************************** */
int Darcy_PK::AdvanceToSteadyState()
{  
  double T = FS->get_time();
  SolveFullySaturatedProblem(T, *solution);
  return 0;
}


/* ******************************************************************
* Calculates steady-state solution assuming that absolute permeability 
* does not depend on time. The boundary conditions are calculated
* only once, during the initialization step.                                                
****************************************************************** */
void Darcy_PK::SolveFullySaturatedProblem(double Tp, Epetra_Vector& u)
{
  solver->SetAztecOption(AZ_output, verbosity_AztecOO);
  solver->SetAztecOption(AZ_conv, AZ_rhs);

  // calculate and assemble elemental stifness matrices
  matrix_->CreateMFDstiffnessMatrices(*Krel_cells, *Krel_faces, FLOW_RELATIVE_PERM_NONE);
  matrix_->CreateMFDrhsVectors();
  AddGravityFluxes_MFD(K, *Krel_cells, *Krel_faces, FLOW_RELATIVE_PERM_NONE, matrix_);
  matrix_->ApplyBoundaryConditions(bc_markers, bc_values);
  matrix_->AssembleGlobalMatrices();
  matrix_->ComputeSchurComplement(bc_markers, bc_values);
  matrix_->UpdatePreconditioner();

  rhs = matrix_->rhs();
  Epetra_Vector b(*rhs);
  solver->SetRHS(&b);  // Aztec00 modifies the right-hand-side.
  solver->SetLHS(&u);  // initial solution guess

  int max_itrs = ti_specs_sss.ls_specs.max_itrs;
  double convergence_tol = ti_specs_sss.ls_specs.convergence_tol;

  solver->Iterate(max_itrs, convergence_tol);

  int num_itrs = solver->NumIters();
  double linear_residual = solver->ScaledResidual();

  if (verbosity >= FLOW_VERBOSITY_HIGH && MyPID == 0) {
    std::printf("Darcy PK: pressure solver(%8.3e, %4d)\n", linear_residual, num_itrs);
  }
}


/* ******************************************************************* 
* Performs one time step of size dT. The boundary conditions are 
* calculated only once, during the initialization step.  
******************************************************************* */
int Darcy_PK::Advance(double dT_MPC) 
{
  dT = dT_MPC;
  double time = FS->get_time();
  if (time >= 0.0) T_physics = time;

  solver->SetAztecOption(AZ_output, verbosity_AztecOO);
  solver->SetAztecOption(AZ_conv, AZ_rhs);

  // update boundary conditions and source terms
  time = T_physics;
  bc_pressure->Compute(time);
  bc_flux->Compute(time);
  bc_seepage->Compute(time);
  if (shift_water_table_.getRawPtr() == NULL) {
    bc_head->Compute(time);
  } else {
    bc_head->ComputeShift(time, shift_water_table_->Values());
  }

  if (src_sink != NULL) {
    if (src_sink_distribution == FLOW_SOURCE_DISTRIBUTION_NONE) { 
      src_sink->Compute(time);
    } else if (src_sink_distribution == FLOW_SOURCE_DISTRIBUTION_VOLUME) {
      src_sink->ComputeDistribute(time);
    } else if (src_sink_distribution == FLOW_SOURCE_DISTRIBUTION_PERMEABILITY) {
      src_sink->ComputeDistribute(time, Kxy->Values());
    } 
  }

  ProcessBoundaryConditions(
      bc_pressure, bc_head, bc_flux, bc_seepage,
      *solution_faces, atm_pressure,
      bc_markers, bc_values);

  // calculate and assemble elemental stifness matrices
  matrix_->CreateMFDstiffnessMatrices(*Krel_cells, *Krel_faces, FLOW_RELATIVE_PERM_NONE);
  matrix_->CreateMFDrhsVectors();
  AddGravityFluxes_MFD(K, *Krel_cells, *Krel_faces, FLOW_RELATIVE_PERM_NONE, matrix_);
  AddTimeDerivativeSpecificStorage(*solution_cells, dT, matrix_);
  AddTimeDerivativeSpecificYield(*solution_cells, dT, matrix_);
  matrix_->ApplyBoundaryConditions(bc_markers, bc_values);
  matrix_->AssembleGlobalMatrices();
  matrix_->ComputeSchurComplement(bc_markers, bc_values);
  matrix_->UpdatePreconditioner();

  rhs = matrix_->rhs();
  if (src_sink != NULL) AddSourceTerms(src_sink, *rhs);

  Epetra_Vector b(*rhs);
  solver->SetRHS(&b);  // Aztec00 modifies the right-hand-side.
  solver->SetLHS(&*solution);  // initial solution guess

  int max_itrs = ti_specs_sss.ls_specs.max_itrs;
  double convergence_tol = ti_specs_sss.ls_specs.convergence_tol;

  solver->Iterate(max_itrs, convergence_tol);
  num_itrs_trs++;

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_HIGH) {
    int num_itrs = solver->NumIters();
    double linear_residual = solver->ScaledResidual();
    std::printf("Darcy PK: pressure solver(%8.3e, %4d)\n", linear_residual, num_itrs);
  }

  // calculate time derivative and 2nd-order solution approximation
  if (ti_specs_sss.dT_method == FLOW_DT_ADAPTIVE) {
    Epetra_Vector& pressure = FS->ref_pressure();  // pressure at t^n

    for (int c = 0; c < ncells_owned; c++) {
      (*pdot_cells)[c] = ((*solution)[c] - pressure[c]) / dT; 
      (*solution)[c] = pressure[c] + ((*pdot_cells_prev)[c] + (*pdot_cells)[c]) * dT / 2;
    }
  }

  // estimate time multiplier
  if (ti_specs_sss.dT_method == FLOW_DT_ADAPTIVE) {
    double err, dTfactor;
    err = ErrorEstimate(&dTfactor);
    if (err > 0.0) throw 1000;  // fix (lipnikov@lan.gov)
    dT_desirable_ = std::min<double>(dT_MPC * dTfactor, ti_specs_sss.dTmax);
  } else {
    dT_desirable_ = std::min<double>(dT_desirable_ * ti_specs_sss.dTfactor, ti_specs_sss.dTmax);
  }
  return 0;
}


/* ******************************************************************
* Transfer data from the external flow state FS_MPC. MPC may request
* to populate the original state FS. 
****************************************************************** */
void Darcy_PK::CommitState(Teuchos::RCP<Flow_State> FS_MPC)
{
  Epetra_Vector& pressure = FS_MPC->ref_pressure();
  pressure = *solution_cells;

  // calculate darcy mass flux
  Epetra_Vector& flux = FS_MPC->ref_darcy_flux();
  matrix_->CreateMFDstiffnessMatrices(*Krel_cells, *Krel_faces, FLOW_RELATIVE_PERM_NONE);
  matrix_->DeriveDarcyMassFlux(*solution, *face_importer_, flux);
  AddGravityFluxes_DarcyFlux(K, *Krel_cells, *Krel_faces, FLOW_RELATIVE_PERM_NONE, flux);
  for (int c = 0; c < nfaces_owned; c++) flux[c] /= rho_;

  // update time derivative
  *pdot_cells_prev = *pdot_cells;

  // DEBUG
  // WriteGMVfile(FS_MPC);
}


/* ******************************************************************
*  Temporary convertion from double to tensor.                                               
****************************************************************** */
void Darcy_PK::SetAbsolutePermeabilityTensor(std::vector<WhetStone::Tensor>& K)
{
  const Epetra_Vector& vertical_permeability = FS->ref_vertical_permeability();
  const Epetra_Vector& horizontal_permeability = FS->ref_horizontal_permeability();

  for (int c = 0; c < K.size(); c++) {
    if (vertical_permeability[c] == horizontal_permeability[c]) {
      K[c].init(dim, 1);
      K[c](0, 0) = vertical_permeability[c];
    } else {
      K[c].init(dim, 2);
      for (int i = 0; i < dim-1; i++) K[c](i, i) = horizontal_permeability[c];
      K[c](dim-1, dim-1) = vertical_permeability[c];
    }
  }
}


/* ******************************************************************
*  Calculate inner product e^T K e in each cell.                                               
****************************************************************** */
void Darcy_PK::CalculatePermeabilityFactorInWell(const std::vector<WhetStone::Tensor>& K, Epetra_Vector& Kxy)
{
  for (int c = 0; c < K.size(); c++) {
    Kxy[c] = 0.0;
    int idim = std::max<int>(1, K[c].size() - 1);
    for (int i = 0; i < idim; i++) Kxy[c] += K[c](i, i);
    Kxy[c] /= idim;
  }
}


/* ******************************************************************
* Adds time derivative related to specific stroage to cell-based 
* part of MFD algebraic system. 
****************************************************************** */
void Darcy_PK::AddTimeDerivativeSpecificStorage(
    Epetra_Vector& pressure_cells, double dT_prec, Matrix_MFD* matrix_operator)
{
  double g = fabs(gravity()[dim - 1]);
  const Epetra_Vector& specific_storage = FS->ref_specific_storage();

  std::vector<double>& Acc_cells = matrix_operator->Acc_cells();
  std::vector<double>& Fc_cells = matrix_operator->Fc_cells();

  for (int c = 0; c < ncells_owned; c++) {
    double volume = mesh_->cell_volume(c);
    double factor = volume * specific_storage[c] / (g * dT_prec);
    Acc_cells[c] += factor;
    Fc_cells[c] += factor * pressure_cells[c];
  }
}


/* ******************************************************************
* Add area/length factor to specific yield. 
****************************************************************** */
void Darcy_PK::UpdateSpecificYield()
{
  // populate ghost cells
#ifdef HAVE_MPI
  Epetra_Vector specific_yield_wghost(mesh_->face_map(true));
  for (int c = 0; c < ncells_owned; c++) specific_yield_wghost[c] = FS->ref_specific_yield()[c];

  FS->CopyMasterCell2GhostCell(specific_yield_wghost);
#else
  Epetra_Vector& specific_yield_wghost = FS->ref_specific_yield();
#endif

  WhetStone::MFD3D mfd3d(mesh_);
  AmanziMesh::Entity_ID_List faces;
  std::vector<int> dirs;

  int negative_yield = 0;
  for (int c = 0; c < ncells_owned; c++) {
    if (specific_yield_wghost[c] > 0.0) {
      mesh_->cell_get_faces_and_dirs(c, &faces, &dirs);

      double area = 0.0;
      int nfaces = faces.size();
      for (int n = 0; n < nfaces; n++) {
        int f = faces[n];
        int c2 = mfd3d.cell_get_face_adj_cell(c, f);

        if (c2 >= 0) {
          if (specific_yield_wghost[c2] <= 0.0)  // cell in the fully saturated layer
            area -= (mesh_->face_normal(f))[dim - 1] * dirs[n];
        }
      }
      FS->ref_specific_yield()[c] *= area;
      if (area <= 0.0) negative_yield++;
    }
  }

#ifdef HAVE_MPI
  int negative_yield_tmp = negative_yield;
  mesh_->get_comm()->MaxAll(&negative_yield_tmp, &negative_yield, 1);
#endif
  if (negative_yield > 0) {
    Errors::Message msg;
    msg << "Darcy PK: configuration of the yield region leads to negative yield interfaces.";
    Exceptions::amanzi_throw(msg);
  }
}


/* ******************************************************************
* Adds time derivative related to specific yiled to cell-based part 
* of MFD algebraic system. Area factor is alreafy inside Sy. 
****************************************************************** */
void Darcy_PK::AddTimeDerivativeSpecificYield(
    Epetra_Vector& pressure_cells, double dT_prec, Matrix_MFD* matrix_operator)
{
  double g = fabs(gravity()[dim - 1]);
  const Epetra_Vector& specific_yield = FS->ref_specific_yield();

  std::vector<double>& Acc_cells = matrix_operator->Acc_cells();
  std::vector<double>& Fc_cells = matrix_operator->Fc_cells();

  for (int c = 0; c < ncells_owned; c++) {
    double factor = specific_yield[c] / (g * dT_prec);
    Acc_cells[c] += factor;
    Fc_cells[c] += factor * pressure_cells[c];
  }
}

}  // namespace AmanziFlow
}  // namespace Amanzi

