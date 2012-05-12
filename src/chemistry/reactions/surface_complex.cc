/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
#include "surface_complex.hh"

#include <iostream>
#include <iomanip>

#include "matrix_block.hh"

namespace amanzi {
namespace chemistry {

SurfaceComplex::SurfaceComplex() {
  species_names_.clear();
  species_ids_.clear();
  stoichiometry_.clear();
  logK_array_.clear();
}  // end SurfaceComplex() constructor

SurfaceComplex::SurfaceComplex(const SpeciesName name,
                               const SpeciesId id,
                               const std::vector<SpeciesName>& species,
                               const std::vector<double>& stoichiometries,
                               const std::vector<int>& species_ids,
                               const double h2o_stoich,
                               const double free_site_stoich,
                               const double charge,
                               const double logK)
    : name_(name),
      identifier_(id),
      charge_(charge),
      surface_concentration_(0.),
      free_site_name_("Unknown"),
      free_site_stoichiometry_(free_site_stoich),
      free_site_id_(-1),
      h2o_stoichiometry_(h2o_stoich),
      lnK_(log_to_ln(logK)),
      lnQK_(0.),
      logK_(logK) {

  species_names_.clear();
  species_ids_.clear();
  stoichiometry_.clear();
  logK_array_.clear();

  set_ncomp(static_cast<int>(stoichiometries.size()));

  // species names
  for (std::vector<SpeciesName>::const_iterator i = species.begin();
       i != species.end(); i++) {
    species_names_.push_back(*i);
  }
  // species stoichiometries
  for (std::vector<double>::const_iterator i = stoichiometries.begin();
       i != stoichiometries.end(); i++) {
    stoichiometry_.push_back(*i);
  }
  // species ids
  for (std::vector<int>::const_iterator i = species_ids.begin();
       i != species_ids.end(); i++) {
    species_ids_.push_back(*i);
  }
}  // end SurfaceComplex() constructor

SurfaceComplex::SurfaceComplex(const SpeciesName name,
                               const SpeciesId id,
                               const std::vector<SpeciesName>& species,
                               const std::vector<double>& stoichiometries,
                               const std::vector<int>& species_ids,
                               const double h2o_stoich,
                               const SpeciesName free_site_name,
                               const double free_site_stoich,
                               const SpeciesId free_site_id,
                               const double charge,
                               const double logK)
    : name_(name),
      identifier_(id),
      charge_(charge),
      surface_concentration_(0.),
      free_site_name_(free_site_name),
      free_site_stoichiometry_(free_site_stoich),
      free_site_id_(free_site_id),
      h2o_stoichiometry_(h2o_stoich),
      lnK_(log_to_ln(logK)),
      lnQK_(0.),
      logK_(logK) {

  species_names_.clear();
  species_ids_.clear();
  stoichiometry_.clear();
  logK_array_.clear();

  set_ncomp(static_cast<int>(stoichiometries.size()));

  // species names
  for (std::vector<SpeciesName>::const_iterator i = species.begin();
       i != species.end(); i++) {
    species_names_.push_back(*i);
  }
  // species stoichiometries
  for (std::vector<double>::const_iterator i = stoichiometries.begin();
       i != stoichiometries.end(); i++) {
    stoichiometry_.push_back(*i);
  }
  // species ids
  for (std::vector<int>::const_iterator i = species_ids.begin();
       i != species_ids.end(); i++) {
    species_ids_.push_back(*i);
  }
}  // end SurfaceComplex() constructor

SurfaceComplex::~SurfaceComplex() {
}  // end SurfaceComplex() destructor

void SurfaceComplex::Update(const std::vector<Species>& primarySpecies,
                            const SurfaceSite& surface_site) {
  double lnQK_temp = -lnK_;

  // Need to consider activity of water

  for (int i = 0; i < ncomp_; i++) {
    lnQK_temp += stoichiometry_[i] *
        primarySpecies[species_ids_[i]].ln_activity();
  }
  lnQK_temp += free_site_stoichiometry() *
      surface_site.ln_free_site_concentration();
  set_lnQK(lnQK_temp);
  set_surface_concentration(std::exp(lnQK()));
}  // end Update()

void SurfaceComplex::AddContributionToTotal(std::vector<double> *total) {
  for (int i = 0; i < ncomp_; i++) {
    (*total)[species_ids_[i]] += stoichiometry_[i] * surface_concentration();
  }
}  // end AddContributionToTotal()

void SurfaceComplex::AddContributionToDTotal(
    const std::vector<Species>& primarySpecies,
    MatrixBlock* dtotal) {
}  // end AddContributionToDTotal()

void SurfaceComplex::Display(void) const {
  std::cout << "    " << name() << " = ";
  std::cout << free_site_stoichiometry_ << " " << free_site_name_ << " + ";
  if (h2o_stoichiometry_ > 0) {
    std::cout << h2o_stoichiometry_ << " " << "H2O" << " + ";
  }
  for (unsigned int i = 0; i < species_names_.size(); i++) {
    std::cout << stoichiometry_[i] << " " << species_names_[i];
    if (i < species_names_.size() - 1) {
      std::cout << " + ";
    }
  }
  std::cout << std::endl;
  std::cout << std::setw(40) << " "
            << std::setw(10) << logK_
            << std::setw(10) << charge()
            << std::endl;
}  // end Display()

void SurfaceComplex::display(void) const {
  std::cout << "    " << name() << " = ";
  std::cout << free_site_stoichiometry_ << " " << free_site_name_ << " + ";
  if (h2o_stoichiometry_ > 0) {
    std::cout << h2o_stoichiometry_ << " " << "H2O" << " + ";
  }
  for (unsigned int i = 0; i < species_names_.size(); i++) {
    std::cout << stoichiometry_[i] << " " << species_names_[i];
    if (i < species_names_.size() - 1) {
      std::cout << " + ";
    }
  }
  std::cout << std::endl;
  std::cout << "     log K: " << logK_
            << "\n     charge: " << charge() << std::endl;
}  // end Display()

void SurfaceComplex::DisplayResultsHeader(void) const {
  std::cout << std::setw(15) << "Complex Name"
            << std::setw(15) << "Concentration"
            << std::endl;
}  // end DisplayResultsHeader()

void SurfaceComplex::DisplayResults(void) const {
  std::cout << std::setw(15) << name()
            << std::scientific << std::setprecision(5)
            << std::setw(15) << surface_concentration()
            << std::endl;
}  // end DisplayResults()

}  // namespace chemistry
}  // namespace amanzi
