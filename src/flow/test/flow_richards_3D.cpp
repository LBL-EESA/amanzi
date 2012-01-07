/*
The flow component of the Amanzi code, richards unit tests.
License: BSD
Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <vector>

#include "UnitTest++.h"

#include "Teuchos_RCP.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_XMLParameterListHelpers.hpp"

#include "Mesh.hh"
#include "MeshFactory.hh"
#include "gmv_mesh.hh"

#include "State.hpp"
#include "Flow_State.hpp"
#include "Richards_PK.hpp"
#include "BDF2_Dae.hpp"


/* **************************************************************** */
TEST(FLOW_3D_RICHARDS) {
  using namespace Teuchos;
  using namespace Amanzi;
  using namespace Amanzi::AmanziMesh;
  using namespace Amanzi::AmanziGeometry;
  using namespace Amanzi::AmanziFlow;

cout << "Test: 3D Richards, crib model" << endl;
  /* read parameter list */
  ParameterList parameter_list;
  string xmlFileName = "test/flow_richards_3D.xml";
  updateParametersFromXmlFile(xmlFileName, &parameter_list);

  // create a mesh framework 
  ParameterList region_list = parameter_list.get<Teuchos::ParameterList>("Regions");
  GeometricModelPtr gm = new GeometricModel(3, region_list);

  ParameterList mesh_list = parameter_list.get<ParameterList>("Mesh");
  ParameterList factory_list = mesh_list.get<ParameterList>("Unstructured").get<ParameterList>("Generate Mesh");
  Epetra_MpiComm comm(MPI_COMM_WORLD);
  MeshFactory mesh_fact(comm);
  Teuchos::RCP<Mesh> mesh(mesh_fact(factory_list, gm));

  // create flow state
  ParameterList state_list = parameter_list.get<ParameterList>("State");
  State S(state_list, mesh);
  Teuchos::RCP<Flow_State> FS = Teuchos::rcp(new Flow_State(S));

  // create Richards process kernel
  ParameterList flow_list = parameter_list.get<ParameterList>("Flow");
  ParameterList rp_list = flow_list.get<ParameterList>("Richards Problem");
  Richards_PK* RPK = new Richards_PK(rp_list, FS);
  RPK->Init();

  // create the initial pressure function
  Epetra_Vector p(RPK->get_super_map());
  Epetra_Vector* pcells = FS->create_cell_view(p);
  Epetra_Vector* pfaces = FS->create_face_view(p);

  p.PutScalar(0.0);
  RPK->applyBoundaryConditions(RPK->get_bc_markers(), RPK->get_bc_values(), *pfaces);

  S.update_pressure(*pcells);
  S.set_time(0.0);

  // solve the problem
  RPK->advance_to_steady_state();
 
  GMV::open_data_file(*mesh, (std::string)"flow.gmv");
  GMV::start_data();
  GMV::write_cell_data(RPK->get_solution_cells(), 0, "pressure");
  GMV::write_cell_data(*(S.get_permeability()), 0, "abs_permeability");
  GMV::write_cell_data(RPK->get_Krel_cells(), 0, "rel_permeability");
  GMV::close_data_file();

  delete RPK;
}
