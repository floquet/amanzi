/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------

License: see $AMANZI_DIR/COPYRIGHT
Author: Ethan Coon

Basic VerboseObject for use by Amanzi code.  Trilinos's VerboseObject is
templated with the class (for no reason) and then requests that the
VerboseObject be inserted as a base class to the using class.  This is serious
code smell (composition over inheritance, especially for code reuse).

I would prefer to get rid of the call to getOSTab(), but I can't figure out
how to do it.

Usage:

class MyClass {
 public:
  MyClass(Teuchos::ParameterList& plist) {
    vo = Teuchos::rcp(new VerboseObject("my_class", plist);
    Teuchos::OSTab tab = vo.getOSTab();

    if (vo.os_OK(Teuchos::VERB_MEDIUM)) {
      *vo.os() << "my string to print" << std::endl;
    }
  }

 protected:
  Teuchos::RCP<VerboseObject> vo;
}


Parameters:

<ParameterList name="my class">
  <ParameterList name="VerboseObject">
    <Parameter name="Verbosity Level" type="string" value="medium"/>
    <Parameter name="Name" type="string" value="my header"/>
    <Parameter name="Hide Line Prefix" type="bool" value="false"/>
    <Parameter name="Write On Rank" type="int" value="0"/>
  </ParameterList>
</ParameterList>


------------------------------------------------------------------------- */

#ifndef AMANZI_VERBOSE_OBJECT_HH_
#define AMANZI_VERBOSE_OBJECT_HH_

#include <string>

#include "Teuchos_ParameterList.hpp"
#include "Teuchos_VerboseObject.hpp"
#include "Epetra_MpiComm.h"


namespace Amanzi {

class VerboseObject : public Teuchos::VerboseObject<VerboseObject> {
 public:
  // Constructors
  VerboseObject(std::string name, Teuchos::ParameterList& plist);
  VerboseObject(const Epetra_MpiComm* const comm, std::string name,
                Teuchos::ParameterList& plist);

  // NOTE: Default destructor, copy construct should be ok.

  // Is process 0 and verbosity is included?
  inline bool os_OK(Teuchos::EVerbosityLevel verbosity);

  // Get the stream (errors if !os_OK()).
  inline Teuchos::RCP<Teuchos::FancyOStream> os();

 public:
  // The default global verbosity level.
  static Teuchos::EVerbosityLevel global_default_level;

  // Show or hide line prefixes
  static bool hide_line_prefix;

  // Size of the left column of names.
  static unsigned int line_prefix_size;

  // Color output for developers
  std::string color(std::string name);
  std::string reset();

 protected:
  Teuchos::RCP<Teuchos::FancyOStream> out_;
  const Epetra_MpiComm* const comm_;

};


bool VerboseObject::os_OK(Teuchos::EVerbosityLevel verbosity) {
  return out_.get() &&
      includesVerbLevel(getVerbLevel(), verbosity, true);
};


Teuchos::RCP<Teuchos::FancyOStream> VerboseObject::os() {
  return out_;
};

}  // namespace Amanzi

#endif
