/*
  Transport PK

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Jeffrey Johnson (jnjohnson@lbl.gov)
*/

#ifndef AMANZI_TRANSPORT_BOUNDARY_FUNCTION_ALQUIMIA_HH_
#define AMANZI_TRANSPORT_BOUNDARY_FUNCTION_ALQUIMIA_HH_

#include <vector>
#include <map>
#include <string>

#include "Teuchos_RCP.hpp"

#include "Mesh.hh"
#include "TabularStringFunction.hh"
#include "TransportDomainFunction.hh"

#ifdef ALQUIMIA_ENABLED
#include "Alquimia_PK.hh"
#include "ChemistryEngine.hh"

namespace Amanzi {
namespace Transport {

template <class ValueType>
class TransportBoundaryFunction_Alquimia : public TransportDomainFunction<ValueType> {
 public:
  TransportBoundaryFunction_Alquimia(const Teuchos::ParameterList& plist,
                                     const Teuchos::RCP<const AmanziMesh::Mesh>& mesh,
                                     Teuchos::RCP<AmanziChemistry::Alquimia_PK> chem_pk,
                                     Teuchos::RCP<AmanziChemistry::ChemistryEngine> chem_engine);

  ~TransportBoundaryFunction_Alquimia()
  {
    chem_engine_->FreeState(alq_mat_props_, alq_state_, alq_aux_data_, alq_aux_output_);
  };

  void Compute(double t_old, double t_new);  

 private:
  void Init_(const std::vector<std::string> &regions);

 private:
  Teuchos::RCP<const AmanziMesh::Mesh> mesh_;

  // iterator methods
  typename std::map<int, ValueType>::iterator begin() { return value_.begin(); }
  typename std::map<int, ValueType>::iterator end() { return value_.end(); }
  typename std::map<int, ValueType>::size_type size() { return value_.size(); }

  std::map<int, ValueType> value_;

  // string function of geochemical conditions
  Teuchos::RCP<TabularStringFunction> f_;

  // Chemistry state and engine.
  Teuchos::RCP<AmanziChemistry::Alquimia_PK> chem_pk_;
  Teuchos::RCP<AmanziChemistry::ChemistryEngine> chem_engine_;

  // Containers for interacting with the chemistry engine.
  AlquimiaState alq_state_;
  AlquimiaMaterialProperties alq_mat_props_;
  AlquimiaAuxiliaryData alq_aux_data_;
  AlquimiaAuxiliaryOutputData alq_aux_output_;

  // A mapping of boundary face indices to interior cells.
  std::map<int, int> cell_for_face_;
};


template <class ValueType>
TransportBoundaryFunction_Alquimia<ValueType> ::
  TransportBoundaryFunction_Alquimia(const Teuchos::ParameterList& plist,
                                     const Teuchos::RCP<const AmanziMesh::Mesh>& mesh,
                                     Teuchos::RCP<AmanziChemistry::Alquimia_PK> chem_pk,
                                     Teuchos::RCP<AmanziChemistry::ChemistryEngine> chem_engine)
    : mesh_(mesh),
      chem_pk_(chem_pk),
      chem_engine_(chem_engine)
    {
      // Check arguments.
      if (chem_engine_ != Teuchos::null) {
        chem_engine_->InitState(alq_mat_props_, alq_state_, alq_aux_data_, alq_aux_output_);
        chem_engine_->GetPrimarySpeciesNames(TransportDomainFunction<ValueType>::tcc_names_);
      } else {
        Errors::Message msg;
        msg << "Geochemistry is off, but a geochemical condition was requested.";
        Exceptions::amanzi_throw(msg); 
      }

      // Get the regions assigned to this geochemical condition. We do not
      // check for region overlaps here, since a better way is to derive from 
      // the generic mesh function.
      std::vector<std::string> regions = plist.get<Teuchos::Array<std::string> >("regions").toVector();
      std::vector<double> times = plist.get<Teuchos::Array<double> >("times").toVector();
      std::vector<std::string> conditions = plist.get<Teuchos::Array<std::string> >("geochemical conditions").toVector();

      // Function of geochemical conditions and the associates regions.
      f_ = Teuchos::rcp(new TabularStringFunction(times, conditions));
      Init_(regions);
    }




template <class ValueType>
void TransportBoundaryFunction_Alquimia<ValueType> :: Compute(double t_old, double t_new)
{

    std::string cond_name = (*f_)(t_new);

    // Loop over sides and evaluate values.
    for (auto it = begin(); it != end(); ++it) {
      // Find the index of the cell we're in.
      int f = it->first; 
      int cell = cell_for_face_[f];

      // Dump the contents of the chemistry state into our Alquimia containers.
      chem_pk_->CopyToAlquimia(cell, alq_mat_props_, alq_state_, alq_aux_data_);

      // Enforce the condition.
      chem_engine_->EnforceCondition(cond_name, t_new, alq_mat_props_, alq_state_, alq_aux_data_, alq_aux_output_);

      // Move the concentrations into place.
      WhetStone::DenseVector& values = it->second;
      for (int i = 0; i < values.NumRows(); i++) {
        values(i) = alq_state_.total_mobile.data[i];
      }
    }

  };

template <class ValueType>
void TransportBoundaryFunction_Alquimia<ValueType> :: Init_(const std::vector<std::string> &regions){

  for (int i = 0; i < regions.size(); ++i) {
    // Get the faces that belong to this region (since boundary conditions
    // are applied on faces).
    assert(mesh_->valid_set_name(regions[i], AmanziMesh::FACE));

    AmanziMesh::Entity_ID_List block;
    mesh_->get_set_entities(regions[i], AmanziMesh::FACE, AmanziMesh::OWNED, &block);
    int nblock = block.size();

    // Now get the cells that are attached to these faces.
    AmanziMesh::Entity_ID_List cells;
    for (int n = 0; n < nblock; ++n) {
      int f = block[n];
      value_[f] = WhetStone::DenseVector(chem_engine_->NumPrimarySpecies());

      mesh_->face_get_cells(f, AmanziMesh::OWNED, &cells);

      cell_for_face_[f] = cells[0];
    }
  }

};


}  // namespace Transport
}  // namespace Amanzi

#endif


#endif
