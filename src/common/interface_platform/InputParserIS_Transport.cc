#include <sstream>
#include <string>

#include "errors.hh"
#include "exceptions.hh"
#include "dbc.hh"

#include "InputParserIS.hh"
#include "InputParserIS_Defs.hh"

namespace Amanzi {
namespace AmanziInput {

/* ******************************************************************
 * Populate Transport parameters.
 ****************************************************************** */
Teuchos::ParameterList InputParserIS::CreateTransportList_(Teuchos::ParameterList* plist)
{
  Errors::Message msg;
  Teuchos::ParameterList trp_list;

  if (plist->isSublist("Execution Control")) {
    Teuchos::ParameterList& exe_list = plist->sublist("Execution Control");
    if (exe_list.isParameter("Transport Model")) {
      if (exe_list.get<std::string>("Transport Model") == "On") {

        // get the expert parameters
        double CFL(1.0);
        if (exe_list.isSublist("Numerical Control Parameters")) {
          Teuchos::ParameterList& ncp_list = exe_list.sublist("Numerical Control Parameters");
          if (ncp_list.isSublist("Unstructured Algorithm")) {
            if (ncp_list.sublist("Unstructured Algorithm").isSublist("Transport Process Kernel")) {
              Teuchos::ParameterList& tpk_list = ncp_list.sublist("Unstructured Algorithm")
                                                         .sublist("Transport Process Kernel");
              if (tpk_list.isParameter("CFL")) {
                CFL = tpk_list.get<double>("CFL");
              }
            }
          }
        }

        // transport is on, set some defaults
        trp_list.set<int>("spatial discretization order", 1);
        trp_list.set<int>("temporal discretization order", 1);
        trp_list.set<double>("CFL", CFL);
        trp_list.set<std::string>("flow mode", "transient");
        trp_list.set<std::string>("advection limiter", "Tensorial");

        trp_list.set<std::string>("solver", "PCG with Hypre AMG");
        trp_list.sublist("VerboseObject") = CreateVerbosityList_(verbosity_level);
        trp_list.set<std::string>("enable internal tests", "no");

        if (exe_list.isSublist("Numerical Control Parameters")) {
          Teuchos::ParameterList& ncp_list = exe_list.sublist("Numerical Control Parameters");
          if (ncp_list.isSublist("Unstructured Algorithm")) {
            Teuchos::ParameterList& ua_list = ncp_list.sublist("Unstructured Algorithm");
            if (ua_list.isSublist("Transport Process Kernel")) {
              Teuchos::ParameterList& tpk_list = ua_list.sublist("Transport Process Kernel");
              if (tpk_list.isParameter("Transport Integration Algorithm")) {
                std::string tia = tpk_list.get<std::string>("Transport Integration Algorithm");

                if (tia == "Explicit First-Order") {
                  trp_list.set<int>("spatial discretization order", 1);
                  trp_list.set<int>("temporal discretization order", 1);
                } else if (tia == "Explicit Second-Order") {
                  trp_list.set<int>("spatial discretization order", 2);
                  trp_list.set<int>("temporal discretization order", 2);
                }
              }
            }
          }
        }

        // now write the dispersion lists if needed
        if (need_dispersion_) {
          Teuchos::ParameterList& d_list = trp_list.sublist("material properties");

          if (plist->isSublist("Material Properties")) {
            Teuchos::ParameterList& mp_list = plist->sublist("Material Properties");
            for (Teuchos::ParameterList::ConstIterator it = mp_list.begin(); it != mp_list.end(); ++it) {
              d_list.set<std::string>("numerical method", "two point flux approximation");

              if ((it->second).isList()) {
                std::string mat_name(it->first);
                Teuchos::ParameterList& mat_list = mp_list.sublist(mat_name);
                Teuchos::ParameterList& disp_list = d_list.sublist(mat_name);

                disp_list.set<std::string>("model", "Bear");  // default
                disp_list.set<Teuchos::Array<std::string> >("regions",
                    mat_list.get<Teuchos::Array<std::string> >("Assigned Regions"));

                if (mat_list.isSublist("Dispersion Tensor: Uniform Isotropic")) {
                  disp_list.set<double>("alphaL", 
                      mat_list.sublist("Dispersion Tensor: Uniform Isotropic").get<double>("alphaL"));
                  disp_list.set<double>("alphaT",
                      mat_list.sublist("Dispersion Tensor: Uniform Isotropic").get<double>("alphaT"));
                }

                if (mat_list.isSublist("Tortuosity Aqueous: Uniform")) {
                  disp_list.set<double>("aqueous tortuosity", 
                     mat_list.sublist("Tortuosity Aqueous: Uniform").get<double>("Value", 0.0));
                }
                if (mat_list.isSublist("Tortuosity Gaseous: Uniform")) {
                  disp_list.set<double>("gaseous tortuosity", 
                     mat_list.sublist("Tortuosity Gaseous: Uniform").get<double>("Value", 0.0));
                }
              }
            }
          }
        }

        // check for molecular diffusion in phases->water list (other solutes are ignored)
        Teuchos::ParameterList& diff_list = trp_list.sublist("molecular diffusion");
        std::vector<std::string> aqueous_names;
        std::vector<double> aqueous_values;

        if (plist->isSublist("Phase Definitions")) {
          Teuchos::ParameterList& pd_list = plist->sublist("Phase Definitions").sublist("Aqueous");
          if (pd_list.isSublist("Phase Components")) {
            Teuchos::ParameterList& pc_list = pd_list.sublist("Phase Components").sublist("Water");

            for (Teuchos::ParameterList::ConstIterator it = pc_list.begin(); it != pc_list.end(); ++it) {
              if ((it->second).isList()) {
                std::string sol_name(it->first);
                Teuchos::ParameterList& pc_sublist = pc_list.sublist(sol_name);
                if (pc_sublist.isParameter("Molecular Diffusivity: Uniform")) {
                  aqueous_names.push_back(sol_name);
                  aqueous_values.push_back(pc_sublist.get<double>("Molecular Diffusivity: Uniform"));
                }
              }
            }
            diff_list.set<Teuchos::Array<std::string> >("aqueous names", aqueous_names);
            diff_list.set<Teuchos::Array<double> >("aqueous values", aqueous_values);
          }
        }

        // check for molecular diffusion in phases->air list
        std::vector<std::string> gaseous_names;
        std::vector<double> gaseous_values;

        if (plist->isSublist("Phase Definitions")) {
          Teuchos::ParameterList& pd_list = plist->sublist("Phase Definitions").sublist("Gaseous");
          if (pd_list.isSublist("Phase Components")) {
            Teuchos::ParameterList& pc_list = pd_list.sublist("Phase Components").sublist("Air");

            for (Teuchos::ParameterList::ConstIterator it = pc_list.begin(); it != pc_list.end(); ++it) {
              if ((it->second).isList()) {
                std::string sol_name(it->first);
                Teuchos::ParameterList& pc_sublist = pc_list.sublist(sol_name);
                if (pc_sublist.isParameter("Molecular Diffusivity: Uniform")) {
                  gaseous_names.push_back(sol_name);
                  gaseous_values.push_back(pc_sublist.get<double>("Molecular Diffusivity: Uniform"));
                }
              }
            }
            diff_list.set<Teuchos::Array<std::string> >("gaseous names", gaseous_names);
            diff_list.set<Teuchos::Array<double> >("gaseous values", gaseous_values);
          }
        }

        // now generate the source lists
        Teuchos::ParameterList src_list = CreateTransportSrcList_(plist);
        if (src_list.begin() != src_list.end()) { // the source lists are not empty
          trp_list.sublist("source terms") = src_list;
        }

        // now generate the boundary conditions
        Teuchos::ParameterList& phase_list = plist->sublist("Phase Definitions");
        Teuchos::ParameterList& bc_list = plist->sublist("Boundary Conditions");

        for (Teuchos::ParameterList::ConstIterator i = bc_list.begin(); i != bc_list.end(); i++) {
          // read the assigned regions
          const std::string name(i->first);
          Teuchos::Array<std::string> regs = bc_list.sublist(name).get<Teuchos::Array<std::string> >("Assigned Regions");
          vv_bc_regions.insert(vv_bc_regions.end(), regs.begin(), regs.end());

          // only count sublists
          if (bc_list.isSublist(name)) {
            if (bc_list.sublist(name).isSublist("Solute BC")) {
              // read the solute bc stuff
              Teuchos::ParameterList& solbc = bc_list.sublist(name).sublist("Solute BC");
              for (Teuchos::ParameterList::ConstIterator ph = solbc.begin(); ph != solbc.end(); ph++) {
                std::string phase_name = ph->first; 
                int n;
                if (phase_name == "Aqueous") { 
                  n = 0;
                } else if (phase_name == "Gaseous") {
                  n = 1;
                } else {
                  msg << "Boundary condition have unsupported phase " << phase_name;
                  Exceptions::amanzi_throw(msg);
                }
                Teuchos::ParameterList& comps = solbc.sublist(phase_name).sublist(phases_[n].solute_name);

                for (std::vector<std::string>::iterator i = comp_names_all_.begin(); i != comp_names_all_.end(); i++) {
                  if (comps.isSublist(*i)) {
                    std::stringstream compss;
                    compss << *i;
                    if (comps.sublist(*i).isSublist("BC: Uniform Concentration")) {
                      Teuchos::ParameterList& bcsub = comps.sublist(*i).sublist("BC: Uniform Concentration");

                      if (bcsub.isParameter("Geochemical Condition")) { 
                        // Add an entry to Transport->boundary conditions->geochemical conditions.
                        Teuchos::ParameterList& gc_list = trp_list.sublist("boundary conditions")
                            .sublist("geochemical conditions");
                        std::string geochem_cond_name = bcsub.get<std::string>("Geochemical Condition");
                        Teuchos::ParameterList& geochem_cond = gc_list.sublist(geochem_cond_name);
                        geochem_cond.set<Teuchos::Array<std::string> >("regions", regs);
                      }
                      else { // ordinary Transport BCs.
                        Teuchos::ParameterList& tbc_list = trp_list.sublist("boundary conditions").sublist("concentration");
                        Teuchos::ParameterList& bc = tbc_list.sublist(compss.str()).sublist(name);
                        bc.set<Teuchos::Array<std::string> >("regions",regs);

                        Teuchos::Array<double> values = bcsub.get<Teuchos::Array<double> >("Values");
                        Teuchos::Array<double> times = bcsub.get<Teuchos::Array<double> >("Times");
                        Teuchos::Array<std::string> time_fns = bcsub.get<Teuchos::Array<std::string> >("Time Functions");

                        Teuchos::ParameterList& bcfn = bc.sublist("boundary concentration").sublist("function-tabular");
                        bcfn.set<Teuchos::Array<double> >("y values", values);
                        bcfn.set<Teuchos::Array<double> >("x values", times);
                        bcfn.set<Teuchos::Array<std::string> >("forms", TranslateForms_(time_fns));
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }

      // remaining global parameters
      trp_list.set<int>("number of aqueous components", phases_[0].solute_comp_names.size());
      trp_list.set<int>("number of gaseous components", phases_[1].solute_comp_names.size());
    }
  }

  return trp_list;
}


/* ******************************************************************
* Sources for transport are placed in the list "src_list".
****************************************************************** */
Teuchos::ParameterList InputParserIS::CreateTransportSrcList_(Teuchos::ParameterList* plist)
{
  Errors::Message msg;
  Teuchos::ParameterList src_list;
  Teuchos::ParameterList& src_sublist = plist->sublist("Sources");

  for (Teuchos::ParameterList::ConstIterator i = src_sublist.begin(); i != src_sublist.end(); i++) {
    const std::string name(i->first);
    if (src_sublist.isSublist(name)) {
      Teuchos::ParameterList& src = src_sublist.sublist(name);

      // get the regions
      Teuchos::Array<std::string> regions = src.get<Teuchos::Array<std::string> >("Assigned Regions");
      vv_src_regions.insert(vv_src_regions.end(), regions.begin(), regions.end());

      std::string dist_method("none");
      if (src.isSublist("Source: Volume Weighted")) {
        dist_method = "volume";
      }
      else if (src.isSublist("Source: Permeability Weighted")) {
        dist_method = "permeability";
      }

      // go to the phase list
      if (src.isSublist("Solute SOURCE")) {
        if (src.sublist("Solute SOURCE").isSublist("Aqueous")) {
          if (src.sublist("Solute SOURCE").sublist("Aqueous").isSublist(phases_[0].solute_name)) {
            Teuchos::ParameterList& pc_list = src.sublist("Solute SOURCE").sublist("Aqueous").sublist(phases_[0].solute_name);

            // loop over all the source definitions
            for (Teuchos::ParameterList::ConstIterator ibc = pc_list.begin(); ibc != pc_list.end(); ibc++) {
              const std::string pc_name(ibc->first);
              Teuchos::ParameterList& solute_src = pc_list.sublist(pc_name);

              // create src sublist
              Teuchos::ParameterList& src_out = src_list.sublist("concentration").sublist(pc_name).sublist("source for " + name);
              src_out.set<Teuchos::Array<std::string> >("regions",regions);

              // get source function
              Teuchos::ParameterList src_fn;
              if (solute_src.isSublist("Source: Uniform Concentration")) {
                src_out.set<std::string>("spatial distribution method","none");
                src_fn = solute_src.sublist("Source: Uniform Concentration");
              }
              else if (solute_src.isSublist("Source: Flow Weighted Concentration")) {
                src_out.set<std::string>("spatial distribution method", dist_method);
                src_fn = solute_src.sublist("Source: Flow Weighted Concentration");
              }
              else {
                msg << "In the definition of Sources: you must either specify 'Source: Uniform"
                    << " Concentration' or 'Source: Flow Weighted Concentration'.";
                Exceptions::amanzi_throw(msg);
              }

              // create time function
              Teuchos::ParameterList& src_out_fn = src_out.sublist("sink");
              Teuchos::Array<double> values = src_fn.get<Teuchos::Array<double> >("Values");
              // write the native time function
              if (values.size() == 1) {
                src_out_fn.sublist("function-constant").set<double>("value", values[0]);
              } else if (values.size() > 1) {
                Teuchos::Array<double> times = src_fn.get<Teuchos::Array<double> >("Times");
                Teuchos::Array<std::string> time_fns = src_fn.get<Teuchos::Array<std::string> >("Time Functions");

                Teuchos::ParameterList& ssofn = src_out_fn.sublist("function-tabular");

                ssofn.set<Teuchos::Array<double> >("x values", times);
                ssofn.set<Teuchos::Array<double> >("y values", values);

                Teuchos::Array<std::string> forms_(time_fns.size());
                for (int i = 0; i < time_fns.size(); i++) {
                  if (time_fns[i] == "Linear") {
                    forms_[i] = "linear";
                  } else if (time_fns[i] == "Constant") {
                    forms_[i] = "constant";
                  } else {
                    msg << "In the definition of Sources: time function can only be 'Linear' or 'Constant'";
                    Exceptions::amanzi_throw(msg);
                  }
                }
                ssofn.set<Teuchos::Array<std::string> >("forms", forms_);
              } else {
                msg << "In the definition of Sources: something is wrong with the input";
                Exceptions::amanzi_throw(msg);
              }
            }
          }
        }
      }
    }
  }

  return src_list;
}

}  // namespace AmanziInput
}  // namespace Amanzi