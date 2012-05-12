/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
#include "chemistry_output.hh"

#include <iostream>
#include <fstream>

#include "chemistry_exception.hh"
#include "chemistry_verbosity.hh"

namespace amanzi {
namespace chemistry {

ChemistryOutput::ChemistryOutput(void)
    : use_stdout_(false),
      file_stream_(NULL) {
  verbosity_flags_.reset();
}  // end ChemistryOutput constructor

ChemistryOutput::~ChemistryOutput() {
  CloseFileStream();
}  // end ChemistryOutput destructor

void ChemistryOutput::Initialize(const OutputOptions& options) {
  verbosity_flags_.reset();
  VerbosityMap verbosity_map = CreateVerbosityMap();
  for (std::vector<std::string>::const_iterator level = options.verbosity_levels.begin();
       level != options.verbosity_levels.end(); ++level) {
    if (verbosity_map.count(*level) > 0) {
      verbosity_flags_.set(verbosity_map.at(*level));
    }
  }
  set_use_stdout(options.use_stdout);
  OpenFileStream(options.file_name);
}  // end Initialize

void ChemistryOutput::OpenFileStream(const std::string& file_name) {
  // close the current file if it exists
  CloseFileStream();
  if (file_name.size()) {
    file_stream_ = new std::ofstream;
    file_stream_->open(file_name.c_str());
    if (!(*file_stream_)) {
      std::ostringstream ost;
      ost << ChemistryException::kChemistryError 
          << "ChemistryOutput::Initialize(): failed to open output file: " 
          << file_name << std::endl;
      throw ChemistryInvalidInput(ost.str());
    }
  }
}  // end OpenFileStream()

void ChemistryOutput::CloseFileStream(void) {
  if (file_stream_) {
    if (file_stream_->is_open()) {
      file_stream_->close();
    }
  }
  //delete file_stream_;
}  // end CloseFileStream()

void ChemistryOutput::Write(const Verbosity level, const std::stringstream& data) {
  //Write(level, data.str().c_str());
  Write(level, data.str());
}  // end Write()

void ChemistryOutput::Write(const Verbosity level, const std::string& data) {
  if (verbosity_flags_.test(level)) {
    if (file_stream_) {
      *file_stream_ << data;
    }
    if (use_stdout()) {
      std::cout << data;
    }
  }
}  // end Write()

}  // namespace chemistry
}  // namespace amanzi