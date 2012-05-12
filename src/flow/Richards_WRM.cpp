/*
This is the flow component of the Amanzi code. 

Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
Amanzi is released under the three-clause BSD License. 
The terms of use and "as is" disclaimer for this license are 
provided in the top-level COPYRIGHT file.

Authors: Neil Carlson (nnc@lanl.gov), 
         Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <string>
#include <vector>

#include "Richards_PK.hpp"

namespace Amanzi {
namespace AmanziFlow {

/* ******************************************************************
* Defines relative permeability ONLY for cells.                                               
****************************************************************** */
void Richards_PK::CalculateRelativePermeabilityCell(const Epetra_Vector& p)
{
  for (int mb = 0; mb < WRM.size(); mb++) {
    std::string region = WRM[mb]->region();

    std::vector<AmanziMesh::Set_ID> block;
    mesh_->get_set_entities(region, AmanziMesh::CELL, AmanziMesh::OWNED, &block);

    std::vector<unsigned int>::iterator i;
    for (i = block.begin(); i != block.end(); i++) {
      double pc = atm_pressure - p[*i];
      (*Krel_cells)[*i] = WRM[mb]->k_relative(pc);
    }
  }
}


/* ******************************************************************
* Wrapper for various ways to define relative permeability of faces.
****************************************************************** */
void Richards_PK::CalculateRelativePermeabilityFace(const Epetra_Vector& p)
{
  CalculateRelativePermeabilityCell(p);  // populates cell-based permeabilities
  FS->copyMasterCell2GhostCell(*Krel_cells);

  if (Krel_method == FLOW_RELATIVE_PERM_UPWIND_GRAVITY) {  // Define K and Krel_faces
    CalculateRelativePermeabilityUpwindGravity(p);

  } else if (Krel_method == FLOW_RELATIVE_PERM_UPWIND_DARCY_FLUX) {
    Epetra_Vector& flux = FS->ref_darcy_flux();
    CalculateRelativePermeabilityUpwindFlux(p, flux);

  } else if (Krel_method == FLOW_RELATIVE_PERM_ARITHMETIC_MEAN) {
    CalculateRelativePermeabilityArithmeticMean(p);
  }
}


/* ******************************************************************
* Defines upwinded relative permeabilities for faces using gravity. 
****************************************************************** */
void Richards_PK::CalculateRelativePermeabilityUpwindGravity(const Epetra_Vector& p)
{
  AmanziMesh::Entity_ID_List faces;
  std::vector<int> dirs;

  Krel_faces->PutScalar(0.0);

  for (int c = 0; c < ncells_wghost; c++) {
    mesh_->cell_get_faces_and_dirs(c, &faces, &dirs);
    int nfaces = faces.size();

    for (int n = 0; n < nfaces; n++) {
      int f = faces[n];
      const AmanziGeometry::Point& normal = mesh_->face_normal(f);
      double cos_angle = (normal * Kgravity_unit[c]) * dirs[n] / mesh_->face_area(f);

      if (cos_angle > FLOW_RELATIVE_PERM_TOLERANCE) {
        (*Krel_faces)[f] = (*Krel_cells)[c];  // The upwind face.
      } else if (bc_markers[f] != FLOW_BC_FACE_NULL) {
        (*Krel_faces)[f] = (*Krel_cells)[c];  // The boundary face.
      } else if (fabs(cos_angle) <= FLOW_RELATIVE_PERM_TOLERANCE) { 
        (*Krel_faces)[f] += (*Krel_cells)[c] / 2;  // Almost vertical face.
      }
    }
  }
}


/* ******************************************************************
* Defines upwinded relative permeabilities for faces using a given flux.
* WARNING: This is the experimental code. 
****************************************************************** */
void Richards_PK::CalculateRelativePermeabilityUpwindFlux(const Epetra_Vector& p,
                                                          const Epetra_Vector& flux)
{
  AmanziMesh::Entity_ID_List faces;
  std::vector<int> dirs;

  Krel_faces->PutScalar(0.0);

  double max_flux;
  flux.MaxValue(&max_flux);
  double tol = FLOW_RELATIVE_PERM_TOLERANCE * max_flux;

  for (int c = 0; c < ncells_wghost; c++) {
    mesh_->cell_get_faces_and_dirs(c, &faces, &dirs);
    int nfaces = faces.size();

    for (int n = 0; n < nfaces; n++) {
      int f = faces[n];

      if (flux[f] * dirs[n] > tol) {
        (*Krel_faces)[f] = (*Krel_cells)[c];  // The upwind face.
      } else if (bc_markers[f] != FLOW_BC_FACE_NULL) {
        (*Krel_faces)[f] = (*Krel_cells)[c];  // The boundary face.
      } else if (fabs(flux[f]) <= tol) { 
        (*Krel_faces)[f] += (*Krel_cells)[c] / 2;  // Almost vertical face.
      }
    }
  }
}


/* ******************************************************************
* Defines relative permeabilities for faces via arithmetic averaging. 
****************************************************************** */
void Richards_PK::CalculateRelativePermeabilityArithmeticMean(const Epetra_Vector& p)
{
  AmanziMesh::Entity_ID_List cells;

  Krel_faces->PutScalar(0.0);
  for (int f = 0; f < nfaces_owned; f++) {
    mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
    int ncells = cells.size();

    for (int n = 0; n < ncells; n++) (*Krel_faces)[f] += (*Krel_cells)[cells[n]];
    (*Krel_faces)[f] /= ncells;
  }
}


/* ******************************************************************
* Use analytical formula for derivative dS/dP.                                               
****************************************************************** */
void Richards_PK::DerivedSdP(const Epetra_Vector& p, Epetra_Vector& ds)
{
  for (int mb = 0; mb < WRM.size(); mb++) {
    std::string region = WRM[mb]->region();
    int ncells = mesh_->get_set_size(region, AmanziMesh::CELL, AmanziMesh::OWNED);

    std::vector<unsigned int> block(ncells);
    mesh_->get_set_entities(region, AmanziMesh::CELL, AmanziMesh::OWNED, &block);

    std::vector<unsigned int>::iterator i;
    for (i = block.begin(); i != block.end(); i++) {
      double pc = atm_pressure - p[*i];
      ds[*i] = -WRM[mb]->dSdPc(pc);  // Negative sign indicates that dSdP = -dSdPc.
    }
  }
}


/* ******************************************************************
* Convertion p -> s.                                               
****************************************************************** */
void Richards_PK::DeriveSaturationFromPressure(const Epetra_Vector& p, Epetra_Vector& s)
{
  for (int mb = 0; mb < WRM.size(); mb++) {
    std::string region = WRM[mb]->region();
    int ncells = mesh_->get_set_size(region, AmanziMesh::CELL, AmanziMesh::OWNED);

    std::vector<unsigned int> block(ncells);
    mesh_->get_set_entities(region, AmanziMesh::CELL, AmanziMesh::OWNED, &block);

    std::vector<unsigned int>::iterator i;
    for (i = block.begin(); i != block.end(); i++) {
      double pc = atm_pressure - p[*i];
      s[*i] = WRM[mb]->saturation(pc);
    }
  }
}


/* ******************************************************************
* Convertion s -> p.                                               
****************************************************************** */
void Richards_PK::DerivePressureFromSaturation(const Epetra_Vector& s, Epetra_Vector& p)
{
  for (int mb = 0; mb < WRM.size(); mb++) {
    std::string region = WRM[mb]->region();
    int ncells = mesh_->get_set_size(region, AmanziMesh::CELL, AmanziMesh::OWNED);

    std::vector<unsigned int> block(ncells);
    mesh_->get_set_entities(region, AmanziMesh::CELL, AmanziMesh::OWNED, &block);

    std::vector<unsigned int>::iterator i;
    for (i = block.begin(); i != block.end(); i++) {
      double pc = WRM[mb]->capillaryPressure(s[*i]);
      p[*i] = atm_pressure - pc;
    }
  }
}


/* ******************************************************************
* Clip pressure using pressure threshold.
****************************************************************** */
void Richards_PK::ClipHydrostaticPressure(const double pmin, Epetra_Vector& p)
{
  for (int mb = 0; mb < WRM.size(); mb++) {
    std::string region = WRM[mb]->region();
    int ncells = mesh_->get_set_size(region, AmanziMesh::CELL, AmanziMesh::OWNED);

    std::vector<unsigned int> block(ncells);
    mesh_->get_set_entities(region, AmanziMesh::CELL, AmanziMesh::OWNED, &block);

    double pc = atm_pressure - pmin;
    double s0 = WRM[mb]->saturation(pc);

    std::vector<unsigned int>::iterator i;
    for (i = block.begin(); i != block.end(); i++) {
      if (p[*i] < pmin) p[*i] = s0;
    }
  }
}


/* ******************************************************************
* Clip pressure using constant saturation.
****************************************************************** */
void Richards_PK::ClipHydrostaticPressure(const double pmin, const double s0, Epetra_Vector& p)
{
  for (int mb = 0; mb < WRM.size(); mb++) {
    std::string region = WRM[mb]->region();
    int ncells = mesh_->get_set_size(region, AmanziMesh::CELL, AmanziMesh::OWNED);

    std::vector<unsigned int> block(ncells);
    mesh_->get_set_entities(region, AmanziMesh::CELL, AmanziMesh::OWNED, &block);

    std::vector<unsigned int>::iterator i;
    for (i = block.begin(); i != block.end(); i++) {
      if (p[*i] < pmin) {
        double pc = WRM[mb]->capillaryPressure(s0);
        p[*i] = atm_pressure - pc;
      }
    }
  }
}


/* ******************************************************************
* Calculates tensor-point product K * g and normalizes the result.
* To minimize parallel communications, the resultin vector Kg_unit 
* is distributed across mesh.
****************************************************************** */
void Richards_PK::CalculateKVectorUnit(const AmanziGeometry::Point& g, 
                                       std::vector<AmanziGeometry::Point>& Kg_unit)
{
  const Epetra_Map& cmap = mesh_->cell_map(true);
  Epetra_MultiVector Kg_copy(cmap, dim);  // temporary vector

  for (int c = 0; c < ncells_owned; c++) {
    AmanziGeometry::Point Kg = K[c] * g;
    double Kg_norm = norm(Kg);
 
    for (int i = 0; i < dim; i++) Kg_copy[i][c] = Kg[i] / Kg_norm;
  }

#ifdef HAVE_MPI
  FS->copyMasterMultiCell2GhostMultiCell(Kg_copy);
#endif

  AmanziGeometry::Point Kg(dim);
  Kg_unit.clear();
  for (int c = 0; c < ncells_wghost; c++) {
    for (int i = 0; i < dim; i++) Kg[i] = Kg_copy[i][c];
    Kg_unit.push_back(Kg);
  }
} 

}  // namespace AmanziFlow
}  // namespace Amanzi


