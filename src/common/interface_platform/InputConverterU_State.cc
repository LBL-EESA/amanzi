/*
  Input Converter 

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Authors: Markus Berndt (original version)
           Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <sstream>
#include <string>

// TPLs
#include <xercesc/dom/DOM.hpp>

// Amanzi's
#include "errors.hh"
#include "exceptions.hh"
#include "dbc.hh"

#include "InputConverterU.hh"
#include "InputConverterU_Defs.hh"

namespace Amanzi {
namespace AmanziInput {

XERCES_CPP_NAMESPACE_USE

/* ******************************************************************
* STATE sublist
****************************************************************** */
Teuchos::ParameterList InputConverterU::TranslateState_()
{
  Teuchos::ParameterList out_list;

  if (vo_->getVerbLevel() >= Teuchos::VERB_HIGH) {
    *vo_->os() << "Translating state" << std::endl;
  }

  // first we write initial conditions for scalars and vectors, not region-specific
  Teuchos::ParameterList& out_ev = out_list.sublist("field evaluators");
  Teuchos::ParameterList& out_ic = out_list.sublist("initial conditions");

  MemoryManager mm;
  Errors::Message msg;
  char* tagname;
  char* text_content;
  
  // --- gravity
  Teuchos::Array<double> gravity(dim_);
  for (int i = 0; i != dim_-1; ++i) gravity[i] = 0.0;
  gravity[dim_-1] = -GRAVITY_MAGNITUDE;
  out_ic.sublist("gravity").set<Teuchos::Array<double> >("value", gravity);

  // --- viscosity
  bool flag;
  DOMNode* node = GetUniqueElementByTagsString_("phases, liquid_phase, viscosity", flag);
  double viscosity = GetTextContentD_(node, "Pa*s");
  out_ic.sublist("fluid_viscosity").set<double>("value", viscosity);

  // --- constant density
  node = GetUniqueElementByTagsString_("phases, liquid_phase, density", flag);
  rho_ = GetTextContentD_(node, "kg/m^3");
  out_ic.sublist("fluid_density").set<double>("value", rho_);

  out_ic.sublist("mass_density_liquid").sublist("function").sublist("All")
      .set<std::string>("region", "All")
      .set<std::string>("component", "cell")
      .sublist("function").sublist("function-constant")
      .set<double>("value", rho_);

  // --- region specific initial conditions from material properties
  std::map<std::string, int> reg2mat;
  int mat(0);

  DOMNodeList* node_list = doc_->getElementsByTagName(mm.transcode("materials"));
  DOMNodeList* children = node_list->item(0)->getChildNodes();

  for (int i = 0; i < children->getLength(); i++) {
    DOMNode* inode = children->item(i);
    if (DOMNode::ELEMENT_NODE == inode->getNodeType()) {
      std::string mat_name = GetAttributeValueS_(static_cast<DOMElement*>(inode), "name");

      node = GetUniqueElementByTagsString_(inode, "assigned_regions", flag);
      std::vector<std::string> regions = CharToStrings_(mm.transcode(node->getTextContent()));

      // record the material ID for each region that this material occupies
      for (int k = 0; k < regions.size(); k++) {
        if (reg2mat.find(regions[k]) == reg2mat.end()) {
          reg2mat[regions[k]] = mat++;
        } else {
          std::stringstream ss;
          ss << "There is more than one material assigned to region \"" << regions[k] << "\".";
          Exceptions::amanzi_throw(Errors::Message(ss.str().c_str()));
        }
      }

      // create regions string
      std::string reg_str;
      for (std::vector<std::string>::const_iterator it = regions.begin(); it != regions.end(); ++it) {
        reg_str = reg_str + *it;
      }

      // -- porosity: skip if compressibility model was already provided.
      if (!compressibility_) {
        double porosity;
        node = GetUniqueElementByTagsString_(inode, "mechanical_properties, porosity", flag);
        if (flag) {
          TranslateFieldEvaluator_(node, "porosity", "-", reg_str, regions, out_ic, out_ev);
        } else {
          msg << "Porosity element must be specified under mechanical_properties";
          Exceptions::amanzi_throw(msg);
        }
      }

      // -- transport porosity 
      node = GetUniqueElementByTagsString_(inode, "mechanical_properties, transport_porosity", flag);
      if (flag) {
        use_transport_porosity_ = true;
        TranslateFieldEvaluator_(node, "transport_porosity", "-", reg_str, regions, out_ic, out_ev);
      } else if (use_transport_porosity_) {
        msg << "Transport porosity element must be specified for all materials or none.";
        Exceptions::amanzi_throw(msg);
      }

      // -- permeability.
      double perm_x, perm_y, perm_z;
      bool perm_init_from_file(false), conductivity(false);
      std::string perm_file, perm_attribute, perm_format, unit("m^2");

      node = GetUniqueElementByTagsString_(inode, "permeability", flag);
      if (!flag) {
        conductivity = true;
        node = GetUniqueElementByTagsString_(inode, "hydraulic_conductivity", flag);
        unit = "m/s"; 
      }

      // First we get either permeability value or the file name
      int file(0);
      std::string file_name, type, format;
      std::vector<std::string> attr_names;
      double kx, ky, kz;

      kx = GetAttributeValueD_(node, "x", TYPE_NUMERICAL, unit, false, -1.0);
      ky = GetAttributeValueD_(node, "y", TYPE_NUMERICAL, unit, false, -1.0);
      kz = GetAttributeValueD_(node, "z", TYPE_NUMERICAL, unit, false, -1.0);

      type = GetAttributeValueS_(node, "type", TYPE_NONE, false, "");
      if (type == "file") file++;
      // format = GetAttributeValueS_(node, "format", TYPE_NONE, false, "");
      // if (format !="") file++;
      file_name = GetAttributeValueS_(node, "filename", TYPE_NONE, false, "");
      if (file_name != "") file++;
      attr_names = GetAttributeVectorS_(node, "attribute", false);
      if (attr_names.size() > 0) file++;

      if (conductivity) {
        kx *= viscosity / (rho_ * GRAVITY_MAGNITUDE);
        ky *= viscosity / (rho_ * GRAVITY_MAGNITUDE);
        kz *= viscosity / (rho_ * GRAVITY_MAGNITUDE);
      }

      // Second, we copy collected data to XML file.
      // For now permeability is not dumped into checkpoint files.
      Teuchos::ParameterList& permeability_ic = out_ic.sublist("permeability");
      permeability_ic.set<bool>("write checkpoint", false);

      if (file == 2){
        permeability_ic.set<std::string>("restart file", file_name);
        kx = ky = kz = 1.0;
      }
      else if (file == 3) {
        permeability_ic.sublist("exodus file initialization")
            .set<std::string>("file", file_name)
            .set<Teuchos::Array<std::string> >("attributes", attr_names);
        kx = ky = kz = 1.0;
      } else if (file == 0) {
        if (ky < 0) ky = kz;  // x-z system was defined
        Teuchos::ParameterList& aux_list = permeability_ic.sublist("function").sublist(reg_str)
            .set<Teuchos::Array<std::string> >("regions", regions)
            .set<std::string>("component", "cell")
            .sublist("function");
        aux_list.set<int>("number of dofs", dim_)
            .set<std::string>("function type", "composite function");
        aux_list.sublist("dof 1 function").sublist("function-constant").set<double>("value", kx);
        aux_list.sublist("dof 2 function").sublist("function-constant").set<double>("value", ky);
        if (dim_ == 3) {
          aux_list.sublist("dof 3 function").sublist("function-constant").set<double>("value", kz);
        } else {
          kz = 0.0;
        }
      } else {
        ThrowErrorIllformed_("materials", "permeability/hydraulic_conductivity", "file/filename/x/y/z");
      }
      if (kx < 0.0 || ky < 0.0 || kz < 0.0) {
        ThrowErrorIllformed_("materials", "permeability/hydraulic_conductivity", "file/filename/x/y/z");
      }

      // -- specific_yield
      node = GetUniqueElementByTagsString_(inode, "mechanical_properties, specific_yield", flag);
      if (flag) {
        TranslateFieldIC_(node, "specific_yield", "-", reg_str, regions, out_ic, out_ev);
      }

      // -- specific storage
      node = GetUniqueElementByTagsString_(inode, "mechanical_properties, specific_storage", flag);
      if (flag) {
        TranslateFieldIC_(node, "specific_storage", "m^-1", reg_str, regions, out_ic, out_ev);
      }

      // -- particle density
      node = GetUniqueElementByTagsString_(inode, "mechanical_properties, particle_density", flag);
      if (flag) {
        TranslateFieldIC_(node, "particle_density", "kg*m^-3", reg_str, regions, out_ic, out_ev);
      }
    }
  }

  // initialization of fields via the initial_conditions list
  node_list = doc_->getElementsByTagName(mm.transcode("initial_conditions"));
  int nchildren(0);
  if (node_list->getLength() != 0) {
    children = node_list->item(0)->getChildNodes();
    nchildren = children->getLength();
  }

  for (int i = 0; i < nchildren; i++) {
    DOMNode* inode = children->item(i);
    if (DOMNode::ELEMENT_NODE == inode->getNodeType()) {
      node = GetUniqueElementByTagsString_(inode, "assigned_regions", flag);
      text_content = mm.transcode(node->getTextContent());
      std::vector<std::string> regions = CharToStrings_(text_content);

      // create regions string
      std::string reg_str;
      for (std::vector<std::string>::const_iterator it = regions.begin(); it != regions.end(); ++it) {
        reg_str = reg_str + *it;
      }

      // -- uniform pressure
      node = GetUniqueElementByTagsString_(inode, "liquid_phase, liquid_component, uniform_pressure", flag);
      if (flag) {
        double p = GetAttributeValueD_(static_cast<DOMElement*>(node), "value");

        Teuchos::ParameterList& pressure_ic = out_ic.sublist("pressure");
        pressure_ic.sublist("function").sublist(reg_str)
            .set<Teuchos::Array<std::string> >("regions", regions)
            .set<std::string>("component", "cell")
            .sublist("function").sublist("function-constant")
            .set<double>("value", p);
      }

      // -- linear pressure
      node = GetUniqueElementByTagsString_(inode, "liquid_phase, liquid_component, linear_pressure", flag);
      if (flag) {
        double p = GetAttributeValueD_(node, "value", "Pa");
        std::vector<double> grad = GetAttributeVectorD_(node, "gradient", "Pa/m");
        std::vector<double> refc = GetAttributeVectorD_(node, "reference_coord", "m");

        grad.insert(grad.begin(), 0.0);
        refc.insert(refc.begin(), 0.0);

        Teuchos::ParameterList& pressure_ic = out_ic.sublist("pressure");
        pressure_ic.sublist("function").sublist(reg_str)
            .set<Teuchos::Array<std::string> >("regions", regions)
            .set<std::string>("component", "cell")
            .sublist("function").sublist("function-linear")
            .set<double>("y0", p)
            .set<Teuchos::Array<double> >("x0", refc)
            .set<Teuchos::Array<double> >("gradient", grad);
      }

      // -- uniform saturation
      node = GetUniqueElementByTagsString_(inode, "liquid_phase, liquid_component, uniform_saturation", flag);
      if (flag) {
        double s = GetAttributeValueD_(node, "value", TYPE_NUMERICAL, "-");

        Teuchos::ParameterList& saturation_ic = out_ic.sublist("saturation_liquid");
        saturation_ic.sublist("function").sublist(reg_str)
            .set<Teuchos::Array<std::string> >("regions", regions)
            .set<std::string>("component", "cell")
            .sublist("function").sublist("function-constant")
            .set<double>("value", s);
      }

      // -- linear saturation
      node = GetUniqueElementByTagsString_(inode, "liquid_phase, liquid_component, linear_saturation", flag);
      if (flag) {
        double s = GetAttributeValueD_(node, "value", TYPE_NUMERICAL, "-");
        std::vector<double> grad = GetAttributeVectorD_(node, "gradient", "m^-1");
        std::vector<double> refc = GetAttributeVectorD_(node, "reference_coord", "m");

        grad.insert(grad.begin(), 0.0);
        refc.insert(refc.begin(), 0.0);

        Teuchos::ParameterList& saturation_ic = out_ic.sublist("saturation_liquid");
        saturation_ic.sublist("function").sublist(reg_str)
            .set<Teuchos::Array<std::string> >("regions", regions)
            .set<std::string>("component", "cell")
            .sublist("function").sublist("function-linear")
            .set<double>("y0", s)
            .set<Teuchos::Array<double> >("x0", refc)
            .set<Teuchos::Array<double> >("gradient", grad);
      }

      // -- darcy_flux
      node = GetUniqueElementByTagsString_(inode, "liquid_phase, liquid_component, velocity", flag);
      if (flag) {
        std::vector<double> velocity;
        velocity.push_back(GetAttributeValueD_(static_cast<DOMElement*>(node), coords_[0].c_str()));
        velocity.push_back(GetAttributeValueD_(static_cast<DOMElement*>(node), coords_[1].c_str()));
        if (dim_ == 3) velocity.push_back(GetAttributeValueD_(static_cast<DOMElement*>(node), coords_[2].c_str()));

        Teuchos::ParameterList& darcy_flux_ic = out_ic.sublist("darcy_flux");
        Teuchos::ParameterList& tmp_list =
            darcy_flux_ic.set<bool>("dot with normal", true)
            .sublist("function").sublist(reg_str)
            .set<Teuchos::Array<std::string> >("regions", regions)
            .set<std::string>("component", "face")
            .sublist("function")
            .set<int>("number of dofs", dim_)
            .set<std::string>("function type", "composite function");

        for (int k = 0; k != dim_; ++k) {
          std::stringstream dof_str;
          dof_str << "dof " << k+1 << " function";
          tmp_list.sublist(dof_str.str()).sublist("function-constant")
                                         .set<double>("value", velocity[k]);
        }
      }

      // -- total_component_concentration (liquid phase)
      int ncomp_l = phases_["water"].size();
      int ncomp_g = phases_["air"].size();
      int ncomp_all = ncomp_l + ncomp_g;

      node = GetUniqueElementByTagsString_(inode, "liquid_phase, solute_component", flag);
      if (flag && ncomp_all > 0) {
        std::vector<double> vals(ncomp_l, 0.0);

        DOMNodeList* children = node->getChildNodes();
        for (int j = 0; j < children->getLength(); ++j) {
          DOMNode* jnode = children->item(j);
          tagname = mm.transcode(jnode->getNodeName());

          if (strcmp(tagname, "uniform_conc") == 0) {
            std::string unit, text;
            text = GetAttributeValueS_(static_cast<DOMElement*>(jnode), "name");
            int m = GetPosition_(phases_["water"], text);
            GetAttributeValueD_(jnode, "value", TYPE_NUMERICAL, "molar");  // just a check
            vals[m] = ConvertUnits_(GetAttributeValueS_(jnode, "value"), unit, solute_molar_mass_[text]);
          }
        }

        Teuchos::ParameterList& tcc_ic = out_ic.sublist("total_component_concentration");
        Teuchos::ParameterList& dof_list = tcc_ic.sublist("function").sublist(reg_str)
            .set<Teuchos::Array<std::string> >("regions", regions)
            .set<std::string>("component", "cell")
            .sublist("function")
            .set<int>("number of dofs", ncomp_all)
            .set<std::string>("function type", "composite function");

        for (int k = 0; k < ncomp_l; k++) {
          std::string name = phases_["water"][k];
          std::stringstream dof_str;
          dof_str << "dof " << k + 1 << " function";
          dof_list.sublist(dof_str.str()).sublist("function-constant").set<double>("value", vals[k]);
        }
      }

      // -- total_component_concentration (gas phase)
      node = GetUniqueElementByTagsString_(inode, "gas_phase, solute_component", flag);
      if (flag) {
        std::vector<double> vals(ncomp_g, 0.0);

        DOMNodeList* children = node->getChildNodes();
        for (int j = 0; j < children->getLength(); ++j) {
          DOMNode* jnode = children->item(j);
          tagname = mm.transcode(jnode->getNodeName());

          if (strcmp(tagname, "uniform_conc") == 0) {
            std::string text = GetAttributeValueS_(static_cast<DOMElement*>(jnode), "name");
            int m = GetPosition_(phases_["air"], text);
            vals[m] = GetAttributeValueD_(static_cast<DOMElement*>(jnode), "value");
          }
        }

        Teuchos::ParameterList& tcc_ic = out_ic.sublist("total_component_concentration");
        Teuchos::ParameterList& dof_list = tcc_ic.sublist("function").sublist(reg_str).sublist("function");
        for (int k = 0; k < ncomp_g; k++) {
          std::string name = phases_["air"][k];
          std::stringstream dof_str;
          dof_str << "dof " << ncomp_l + k + 1 << " function";
          dof_list.sublist(dof_str.str()).sublist("function-constant").set<double>("value", vals[k]);
        }
      }

      // -- uniform temperature
      node = GetUniqueElementByTagsString_(inode, "uniform_temperature", flag);
      if (flag) {
        double val = GetAttributeValueD_(static_cast<DOMElement*>(node), "value");

        Teuchos::ParameterList& temperature_ic = out_ic.sublist("temperature");
        temperature_ic.sublist("function").sublist(reg_str)
            .set<Teuchos::Array<std::string> >("regions", regions)
            .set<std::string>("component", "cell")
            .sublist("function").sublist("function-constant")
            .set<double>("value", val);
      }

      // -- geochemical condition
      node = GetUniqueElementByTagsString_(inode, "liquid_phase, geochemistry_component, constraint", flag);
      if (flag) {
        std::string name = GetAttributeValueS_(static_cast<DOMElement*>(node), "name");

        out_ic.sublist("geochemical conditions").sublist(name)
            .set<Teuchos::Array<std::string> >("regions", regions);

        TranslateStateICsAmanziGeochemistry_(out_ic, name, regions);
      }
    }
  }

  // atmospheric pressure
  out_ic.sublist("atmospheric_pressure").set<double>("value", ATMOSPHERIC_PRESSURE);

  // add mesh partitions to the state list
  out_list.sublist("mesh partitions") = TranslateMaterialsPartition_();

  // visualization blacklist and whitelist
  node = GetUniqueElementByTagsString_("output, vis, blacklist", flag);
  if (flag) {
    text_content = mm.transcode(node->getTextContent());
    out_list.set<Teuchos::Array<std::string> >("blacklist", CharToStrings_(text_content));
  }

  node = GetUniqueElementByTagsString_("output, vis, whitelist", flag);
  if (flag) {
    text_content = mm.transcode(node->getTextContent());
    out_list.set<Teuchos::Array<std::string> >("whitelist", CharToStrings_(text_content));
  }


  return out_list;
}


/* ******************************************************************
* Select proper evaluator based on the list of input parameters.
****************************************************************** */
void InputConverterU::TranslateFieldEvaluator_(
    DOMNode* node, std::string field, std::string unit,
    const std::string& reg_str, const std::vector<std::string>& regions,
    Teuchos::ParameterList& out_ic, Teuchos::ParameterList& out_ev)
{
  std::string type = GetAttributeValueS_(node, "type", TYPE_NONE, false, "");
  if (type == "file") {
    std::string filename = GetAttributeValueS_(node, "filename");
    Teuchos::ParameterList& field_ic = out_ic.sublist(field);
    field_ic.set<std::string>("restart file", filename);

    Teuchos::ParameterList& field_ev = out_ev.sublist(field);
    field_ev.set<std::string>("field evaluator type", "constant variable");
  } else {
    double val = GetAttributeValueD_(node, "value", TYPE_NUMERICAL, unit);

    Teuchos::ParameterList& field_ev = out_ev.sublist(field);
    field_ev.sublist("function").sublist(reg_str)
        .set<Teuchos::Array<std::string> >("regions", regions)
        .set<std::string>("component", "cell")
        .sublist("function").sublist("function-constant")
        .set<double>("value", val);
    field_ev.set<std::string>("field evaluator type", "independent variable");
  }
}


/* ******************************************************************
* Select proper IC based on the list of input parameters.
****************************************************************** */
void InputConverterU::TranslateFieldIC_(
    DOMNode* node, std::string field, std::string unit,
    const std::string& reg_str, const std::vector<std::string>& regions,
    Teuchos::ParameterList& out_ic, Teuchos::ParameterList& out_ev)
{
  std::string type = GetAttributeValueS_(node, "type", TYPE_NONE, false, "");
  if (type == "file") {
    std::string filename = GetAttributeValueS_(node, "filename");
    Teuchos::ParameterList& field_ic = out_ic.sublist(field);
    field_ic.set<std::string>("restart file", filename);
  } else {
    double val = GetAttributeValueD_(node, "value", TYPE_NUMERICAL, unit);

    Teuchos::ParameterList& field_ic = out_ic.sublist(field);
    field_ic.sublist("function").sublist(reg_str)
        .set<Teuchos::Array<std::string> >("regions", regions)
        .set<std::string>("component", "cell")
        .sublist("function").sublist("function-constant")
        .set<double>("value", val);
  }
}


/* ******************************************************************
* Mesh patition sublist based on materials
****************************************************************** */
Teuchos::ParameterList InputConverterU::TranslateMaterialsPartition_()
{
  Teuchos::ParameterList out_list;
  Teuchos::ParameterList& tmp_list = out_list.sublist("materials");

  MemoryManager mm;
  bool flag;
  std::vector<std::string> regions;

  DOMNodeList* node_list = doc_->getElementsByTagName(mm.transcode("materials"));
  DOMNodeList* children = node_list->item(0)->getChildNodes();

  for (int i = 0; i < children->getLength(); i++) {
    DOMNode* inode = children->item(i);
    if (DOMNode::ELEMENT_NODE == inode->getNodeType()) {
      DOMNamedNodeMap* attr_map = inode->getAttributes();

      DOMNode* node = GetUniqueElementByTagsString_(inode, "assigned_regions", flag);
      if (flag) {
        char* text_content = mm.transcode(node->getTextContent());
        std::vector<std::string> names = CharToStrings_(text_content);

        for (int i = 0; i < names.size(); i++) {
          regions.push_back(names[i]);
        } 
      }
    }
  }
  tmp_list.set<Teuchos::Array<std::string> >("region list", regions);
  
  return out_list;
}


/* ******************************************************************
* Create initialization list for concentration. This routine is called
* when geochemistry list exists for initial conditions.
****************************************************************** */
void InputConverterU::TranslateStateICsAmanziGeochemistry_(
    Teuchos::ParameterList& out_list, std::string& constraint,
    std::vector<std::string>& regions)
{
  if (vo_->getVerbLevel() >= Teuchos::VERB_HIGH) {
    Teuchos::OSTab tab = vo_->getOSTab();
    *vo_->os() << "Compatibility mode: translating ICs for native chemistry" << std::endl;
  }

  bool flag;
  DOMNode* node;
  DOMElement* element;

  node = GetUniqueElementByTagsString_("process_kernels, chemistry", flag);
  std::string engine = GetAttributeValueS_(static_cast<DOMElement*>(node), "engine");

  node = GetUniqueElementByTagsString_("geochemistry, constraints", flag);
  if (flag && engine == "amanzi") {
    std::string name;
    element = GetUniqueChildByAttribute_(node, "name", constraint, flag, true);
    std::vector<DOMNode*> children = GetSameChildNodes_(element, name, flag);
    if (children.size() != phases_["water"].size()) {
      Errors::Message msg;
      msg << "Constraint \"" << constraint << "\" is not backward compatible: "
          << " check the number of components.";
      Exceptions::amanzi_throw(msg);
    }

    Teuchos::ParameterList& ic_list = out_list.sublist("total_component_concentration")
        .sublist("function").sublist("All");

    ic_list.set<Teuchos::Array<std::string> >("regions", regions)
        .set<std::string>("component", "cell");

    Teuchos::ParameterList& tmp_list = ic_list.sublist("function")
        .set<int>("number of dofs", children.size())
        .set<std::string>("function type", "composite function");

    for (int i = 0; i < children.size(); ++i) {
      std::string species = GetAttributeValueS_(children[i], "name");
      double val = GetAttributeValueD_(children[i], "value", TYPE_NUMERICAL);

      // find position of species in the list of component names
      int k(-1);
      for (int n = 0; n < comp_names_all_.size(); ++n) {
        if (comp_names_all_[n] == species) {
          k = n;
          break;
        }
      }

      std::stringstream dof_str;
      dof_str << "dof " << k+1 << " function";
      tmp_list.sublist(dof_str.str()).sublist("function-constant")
                                     .set<double>("value", val);
    }
  }
}

}  // namespace AmanziInput
}  // namespace Amanzi
