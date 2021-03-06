/*
  Operators

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)
          Ethan Coon (ecoon@lanl.gov)

  Adapted from operator_marshak, this does marshak on a logical mesh.
  More of a test for logical meshes than a test for operators. --etc
*/

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// TPLs
#include "Teuchos_RCP.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_ParameterXMLFileReader.hpp"
#include "UnitTest++.h"

// Amanzi
#include "MeshLogicalFactory.hh"
#include "MeshLogical.hh"
#include "mfd3d_diffusion.hh"
#include "Tensor.hh"

// Operators
#include "LinearOperatorFactory.hh"
#include "OperatorDefs.hh"
#include "Operator.hh"
#include "OperatorAccumulation.hh"
#include "OperatorDiffusionFV.hh"
#include "UpwindFlux.hh"

#include "operator_marshak_testclass.hh"


void RunTestMarshakLogical(std::string op_list_name) {
  using namespace Teuchos;
  using namespace Amanzi;
  using namespace Amanzi::AmanziMesh;
  using namespace Amanzi::AmanziGeometry;
  using namespace Amanzi::Operators;

  Epetra_MpiComm comm(MPI_COMM_WORLD);
  int MyPID = comm.MyPID();

  if (MyPID == 0) std::cout << "\nTest: Simulating nonlinear Marshak wave" << std::endl;

  // read parameter list
  std::string xmlFileName = "test/operator_marshak_logical.xml";
  ParameterXMLFileReader xmlreader(xmlFileName);
  ParameterList plist = xmlreader.getParameters();

  // create a logical mesh
  ParameterList region_list = plist.get<Teuchos::ParameterList>("regions");
  Teuchos::RCP<GeometricModel> gm = Teuchos::rcp(new GeometricModel(3, region_list, &comm));

  MeshLogicalFactory fac(&comm, gm);

  AmanziGeometry::Point begin(0.,0.5,0.5), end(1.,0.5,0.5);
  AmanziMesh::Entity_ID_List cells,faces;
  double length;
  fac.AddSegment(100,begin,end,1.0,
		 MeshLogicalFactory::TIP_BOUNDARY,MeshLogicalFactory::TIP_BOUNDARY,
		 "myregion",&cells, &faces, &length);
  RCP<const Mesh> mesh = fac.Create();

  // Create nonlinear coefficient.
  Teuchos::RCP<HeatConduction> knc = Teuchos::rcp(new HeatConduction(mesh, 0.0));

  // modify diffusion coefficient
  // -- since rho=mu=1.0, we do not need to scale the diffusion coefficient.
  Teuchos::RCP<std::vector<WhetStone::Tensor> > K = Teuchos::rcp(new std::vector<WhetStone::Tensor>());
  int ncells_owned = mesh->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  int nfaces_wghost = mesh->num_entities(AmanziMesh::FACE, AmanziMesh::USED);

  for (int c = 0; c < ncells_owned; c++) {
    WhetStone::Tensor Kc(2, 1);
    Kc(0, 0) = 1.0;
    K->push_back(Kc);
  }
  double rho(1.0), mu(1.0);

  // create boundary data (no mixed bc)
  std::vector<int> bc_model(nfaces_wghost, OPERATOR_BC_NONE);
  std::vector<double> bc_value(nfaces_wghost);
  std::vector<double> bc_mixed;

  for (int f = 0; f < nfaces_wghost; f++) {
    const Point& xf = mesh->face_centroid(f);

    if (fabs(xf[1]) < 1e-6 || fabs(xf[1] - 1.0) < 1e-6) {
      bc_model[f] = Operators::OPERATOR_BC_NEUMANN;
      bc_value[f] = 0.0;
    } else if(fabs(xf[0]) < 1e-6) {
      bc_model[f] = Operators::OPERATOR_BC_DIRICHLET;
      bc_value[f] = knc->TemperatureSource;
    } else if(fabs(xf[0] - 1.0) < 1e-6) {
      bc_model[f] = Operators::OPERATOR_BC_DIRICHLET;
      bc_value[f] = knc->TemperatureFloor;
    }
  }
  Teuchos::RCP<BCs> bc = Teuchos::rcp(new BCs(OPERATOR_BC_TYPE_FACE, bc_model, bc_value, bc_mixed));

  // create solution map.
  Teuchos::RCP<CompositeVectorSpace> cvs = Teuchos::rcp(new CompositeVectorSpace());
  cvs->SetMesh(mesh);
  cvs->SetGhosted(true);
  cvs->SetComponent("cell", AmanziMesh::CELL, 1);
  cvs->SetOwned(false);
  cvs->AddComponent("face", AmanziMesh::FACE, 1);

  // create and initialize state variables.
  Teuchos::RCP<CompositeVector> flux = Teuchos::rcp(new CompositeVector(*cvs));
  Epetra_MultiVector& flx = *flux->ViewComponent("face", true);

  Point velocity(0.0, 0.0);
  for (int f = 0; f < nfaces_wghost; f++) {
    const Point& normal = mesh->face_normal(f);
    flx[0][f] = velocity * normal;
  }

  CompositeVector solution(*cvs);
  solution.PutScalar(knc->TemperatureFloor);  // solution at time T=0

  CompositeVector heat_capacity(*cvs);
  heat_capacity.PutScalar(1.0);

  // Create upwind model
  ParameterList& ulist = plist.sublist("PK operator").sublist("upwind");
  UpwindFlux<HeatConduction> upwind(mesh, knc);
  upwind.Init(ulist);

  // MAIN LOOP
  int step(0);
  double snorm(0.0);
  
  double T(0.0), dT(1e-4);
  while (T < 1.0) {
    solution.ScatterMasterToGhosted();

    // update bc
    for (int f = 0; f < nfaces_wghost; f++) {
      const Point& xf = mesh->face_centroid(f);
      if(fabs(xf[0]) < 1e-6) bc_value[f] = knc->exact(T + dT, xf);
    }

    // upwind heat conduction coefficient
    knc->UpdateValues(solution);
    ModelUpwindFn func = &HeatConduction::Conduction;
    upwind.Compute(*flux, solution, bc_model, bc_value, *knc->values(), func);

    // add diffusion operator
    Teuchos::ParameterList olist = plist.sublist("PK operator").sublist(op_list_name);
    OperatorDiffusionFV op(olist, mesh);
    op.SetBCs(bc, bc);

    int schema_dofs = op.schema_dofs();
    int schema_prec_dofs = op.schema_prec_dofs();

    op.Setup(K, knc->values(), knc->derivatives());
    op.UpdateMatrices(flux.ptr(), Teuchos::null);

    // get the global operator
    Teuchos::RCP<Operator> global_op = op.global_operator();

    // add accumulation terms
    OperatorAccumulation op_acc(AmanziMesh::CELL, global_op);
    op_acc.AddAccumulationTerm(solution, heat_capacity, dT, "cell");

    // apply BCs and assemble
    op.ApplyBCs(true, true);
    global_op->SymbolicAssembleMatrix();
    global_op->AssembleMatrix();

    // create preconditoner
    ParameterList slist = plist.get<Teuchos::ParameterList>("preconditioners");
    global_op->InitPreconditioner("Hypre AMG", slist);

    // solve the problem
    ParameterList lop_list = plist.get<Teuchos::ParameterList>("solvers");
    AmanziSolvers::LinearOperatorFactory<Operator, CompositeVector, CompositeVectorSpace> factory;
    Teuchos::RCP<AmanziSolvers::LinearOperator<Operator, CompositeVector, CompositeVectorSpace> >
       solver = factory.Create("Amanzi GMRES", lop_list, global_op);

    Epetra_MultiVector& sol_new = *solution.ViewComponent("cell");
    Epetra_MultiVector sol_old(sol_new);

    CompositeVector rhs = *global_op->rhs();
    solver->add_criteria(AmanziSolvers::LIN_SOLVER_MAKE_ONE_ITERATION);
    int ierr = solver->ApplyInverse(rhs, solution);

    step++;
    T += dT;

    solution.ViewComponent("cell")->Norm2(&snorm);

    if (MyPID == 0) {
      printf("%3d  ||r||=%11.6g  itr=%2d  ||sol||=%11.6g  T=%7.4f  dT=%7.4f\n",
          step, solver->residual(), solver->num_itrs(), snorm, T, dT);
    }

    // change time step
    Epetra_MultiVector sol_diff(sol_old);
    sol_diff.Update(1.0, sol_new, -1.0);

    double ds_max, ds_rel(0.0);
    for (int c = 0; c < ncells_owned; c++) {
      ds_rel = std::max(ds_rel, sol_diff[0][c] / (1e-3 + sol_old[0][c] + sol_new[0][c]));
    }
    double ds_rel_local = ds_rel;
    sol_diff.Comm().MaxAll(&ds_rel_local, &ds_rel, 1);

    if (ds_rel < 0.05) {
      dT *= 1.2;
    } else if (ds_rel > 0.10) {
      dT *= 0.8;
    }
    // dT = std::min(dT, 0.002);
  }

  // calculate errors
  const Epetra_MultiVector& p = *solution.ViewComponent("cell");
  double pl2_err(0.0), pnorm(0.0);

  for (int c = 0; c < ncells_owned; ++c) {
    const AmanziGeometry::Point& xc = mesh->cell_centroid(c);
    double err = p[0][c] - knc->exact(T, xc);
    pl2_err += err * err;
    pnorm += p[0][c] * p[0][c];
  }
  pl2_err = std::pow(pl2_err / pnorm, 0.5);
  pnorm = std::pow(pnorm, 0.5);
  printf("||dp||=%10.6g  ||p||=%10.6g\n", pl2_err, pnorm);

  CHECK_CLOSE(0., pl2_err, 0.1);
}


TEST(MARSHAK_NONLINEAR_WAVE_LOGICAL) {
  RunTestMarshakLogical("diffusion operator");
}

