/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
#ifndef __ACTIVITY_MODEL_HPP__
#define __ACTIVITY_MODEL_HPP__

// Base class for activity calculations

#include <vector>

#include "Species.hpp"
#include "AqueousEquilibriumComplex.hpp"

class ActivityModel {
 public:
  ActivityModel();
  virtual ~ActivityModel();

  void CalculateIonicStrength(std::vector<Species> primarySpecies,
                              std::vector<AqueousEquilibriumComplex> secondarySpecies);
  void CalculateActivityCoefficients(std::vector<Species> &primarySpecies,
                                     std::vector<AqueousEquilibriumComplex> &secondarySpecies);
  virtual double Evaluate(const Species& species) = 0;

  double ionic_strength(void) const { return this->I_; }

  virtual void Display(void) const = 0;

  void name(const std::string name) { this->name_ = name; };
  std::string name(void) { return this->name_; };

 protected:
  double log_to_ln(double d) { return d*2.30258509299; }
  double ln_to_log(double d) { return d*0.434294481904; }

  void ionic_strength(double d) { this->I_ = d; }

  double I_;  // ionic strength

 private:
  std::string name_;
};

#endif  // __ACTIVITY_MODEL_HPP__

