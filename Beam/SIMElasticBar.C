// $Id$
//==============================================================================
//!
//! \file SIMElasticBar.C
//!
//! \date Aug 11 2013
//!
//! \author Knut Morten Okstad / SINTEF
//!
//! \brief Solution driver for NURBS-based FE analysis of elastic bars & beams.
//!
//==============================================================================

#include "SIMElasticBar.h"
#include "ElasticCable.h"
#include "ElasticBeam.h"
#include "BeamProperty.h"
#include "AlgEqSystem.h"
#include "ASMs1D.h"
#include "SAM.h"
#include "AnaSol.h"
#include "Functions.h"
#include "Utilities.h"
#include "Vec3Oper.h"
#include "IFEM.h"
#include "tinyxml2.h"
#include <cstring>


SIMElasticBar::SIMElasticBar (const char* hd, unsigned char n) : SIMElastic1D(3)
{
  nsd = 3;
  nsv = n;
  twist = nullptr;
  printed = false;
  lcStiff = false;
  if (hd) myHeading = hd;
}


SIMElasticBar::~SIMElasticBar ()
{
  delete twist;
  for (BeamProperty* prop : myBCSec)
    delete prop;
  for (PointLoad& load : myLoads)
    delete load.p;
}


bool SIMElasticBar::printProblem () const
{
  // Avoid printing problem definition more than once
  if (printed) return false;

  return printed = this->SIM1D::printProblem();
}


ElasticBar* SIMElasticBar::getBarIntegrand (const std::string& type)
{
  if (type == "cable")
    myProblem = new ElasticCable(nsd,nsv);
  else
    myProblem = new ElasticBar(toupper(type[0]),nsd);

  // Always one integration point per element
  if (type != "cable")
    opt.nGauss[0] = opt.nGauss[1] = 1;

  return dynamic_cast<ElasticBar*>(myProblem);
}


ElasticBeam* SIMElasticBar::getBeamIntegrand (const std::string&)
{
  myProblem = new ElasticBeam(nsv);

  // Always one integration point per element
  opt.nGauss[0] = opt.nGauss[1] = 1;

  return dynamic_cast<ElasticBeam*>(myProblem);
}


const char** SIMElasticBar::getPrioritizedTags () const
{
  static std::vector<const char*> special;
  if (special.empty())
  {
    for (const char** p = this->SIMElastic1D::getPrioritizedTags(); *p; ++p)
    {
      // The <beam> tag has to be parsed before the <geometry> tag,
      // in order to generate array of nodal rotation matrices
      if (!strcmp(*p,"geometry"))
        special.push_back("beam");
      special.push_back(*p);
    }
    special.push_back(nullptr);
  }

  return special.data();
}


bool SIMElasticBar::parse (const tinyxml2::XMLElement* elem)
{
  bool isCable = !strcasecmp(elem->Value(),"cable");
  if (isCable || !strcasecmp(elem->Value(),"bar"))
  {
    nf = 3;
    utl::getAttribute(elem,"loadCorrectionStiffness",lcStiff,true);
  }
  else if (!strcasecmp(elem->Value(),"beam"))
    nf = 6;
  else if (!strcasecmp(elem->Value(),"anasol"))
  {
    std::string type("expression");
    utl::getAttribute(elem,"type",type,true);
    if (type == "expression")
    {
      IFEM::cout <<"\tAnalytical solution: Expression"<< std::endl;
      if (!mySol) mySol = new AnaSol(elem,false);
    }
    else
      std::cerr <<"  ** SIMElasticBar::parse: Invalid analytical solution "
                << type <<" (ignored)"<< std::endl;
    return true;
  }
  else
    return this->SIM1D::parse(elem);

  ElasticBar*  bar  = nullptr;
  ElasticBeam* beam = nullptr;
  if (myProblem)
    switch (nf) {
    case 3:
      bar = dynamic_cast<ElasticBar*>(myProblem);
      break;
    case 6:
      beam = dynamic_cast<ElasticBeam*>(myProblem);
      break;
    }
  else
  {
    std::string type(isCable ? "cable" : "N");
    utl::getAttribute(elem,"type",type,true);
    switch (nf) {
    case 3:
      bar = this->getBarIntegrand(type);
      break;
    case 6:
      beam = this->getBeamIntegrand(type);
      break;
    }
  }

  if (!bar && !beam)
    return false;

  bool ok = true;
  const tinyxml2::XMLElement* child = elem->FirstChildElement();
  for (; child && ok; child = child->NextSiblingElement())
  {
    IFEM::cout <<"  Parsing <"<< child->Value() <<">"<< std::endl;
    if (!strcasecmp(child->Value(),"material"))
    {
      double E = 2.1e11, rho = 7.85e3;
      utl::getAttribute(child,"E",E);
      utl::getAttribute(child,"rho",rho);
      if (bar)
      {
        double A = 1.0, I = 0.0, EA = 0.0, EI = 0.0, rhoA = 0.0;
        BeamProperty::parsePipe(child,A,I); // "D" or "R" and optionally "t"
        if (!utl::getAttribute(child,"EA",EA))
        {
          utl::getAttribute(child,"A",A);
          EA = E*A;
        }
        bar->setStiffness(EA);
        if (isCable)
        {
          if (!utl::getAttribute(child,"EI",EI))
          {
            utl::getAttribute(child,"I",I);
            EI = E*I;
          }
          static_cast<ElasticCable*>(bar)->setBendingStiffness(EI);
        }
        if (utl::getAttribute(child,"rhoA",rhoA) && A > 0.0)
          rho = rhoA/A;
        else
          rhoA = rho*A;
        bar->setMass(rhoA);
        IFEM::cout <<"\tAxial stiffness = "<< EA;
        if (isCable && EI > 0.0)
          IFEM::cout <<"\n\tBending stiffness = "<< EI;
      }
      else
      {
        double G = 8.1e10, nu = 0.0;
        if (!utl::getAttribute(child,"G",G) && utl::getAttribute(child,"nu",nu))
          if (nu >= 0.0 && nu < 0.5)
            G = E/(nu+nu+2.0); // Derive G-modulus from E and nu
        beam->setStiffness(E,G);
        beam->setMass(rho);
        IFEM::cout <<"\tStiffness moduli = "<< E <<" "<< G;
      }
      IFEM::cout << (isCable ? "\n\tM" : ", m")
                  <<"ass density = "<< rho << std::endl;
    }

    else if (beam && !strcasecmp(child->Value(),"properties"))
    {
      this->parseMaterialSet(child,myBCSec.size());
      myBCSec.push_back(ElasticBeam::parseProp(child));
      beam->setProperty(myBCSec.back());
    }

    else if (beam && !strcasecmp(child->Value(),"lineload"))
      beam->parseBeamLoad(child);

    else if (beam && !strcasecmp(child->Value(),"cplload"))
      beam->parseBeamLoad(child);

    else if (beam && this->parseTwist(child))
      continue;

    else if (!strcasecmp(child->Value(),"pointload") && child->FirstChild())
    {
      PointLoad load(1);
      utl::getAttribute(child,"dof",load.ldof);
      utl::getAttribute(child,"u",load.xi);
      if (load.ldof > 0 && load.ldof <= nf && load.xi >= 0.0 && load.xi <= 1.0)
      {
        // Negate the local DOF flag to signal that element loads are allowed
        bool allowElementPointLoad = false;
        utl::getAttribute(child,"onElement",allowElementPointLoad);
        if (allowElementPointLoad) load.ldof = -load.ldof;

        if (utl::getAttribute(child,"patch",load.inod))
          IFEM::cout <<"\tPoint: P"<< load.inod;
        else
          IFEM::cout <<"\tPoint:";
        IFEM::cout <<" xi = "<< load.xi <<" dof = "<< load.ldof <<" Load: ";

        std::string type("constant");
        utl::getAttribute(child,"type",type);
        if (type == "constant")
        {
          load.p = new ConstantFunc(atof(child->FirstChild()->Value()));
          IFEM::cout << (*load.p)(0.0) << std::endl;
        }
        else
          load.p = utl::parseTimeFunc(child->FirstChild()->Value(),type);

        myLoads.push_back(load);
      }
    }

    else if (!strcasecmp(child->Value(),"nodeload") && child->FirstChild())
    {
      PointLoad load;
      utl::getAttribute(child,"node",load.inod);
      utl::getAttribute(child,"dof",load.ldof);

      if (load.inod > 0 && load.ldof > 0 && load.ldof <= nf)
      {
        std::string type("constant");
        utl::getAttribute(child,"type",type);

        IFEM::cout <<"\tNode "<< load.inod <<" dof "<< load.ldof <<" Load: ";
        if (type == "constant")
        {
          load.p = new ConstantFunc(atof(child->FirstChild()->Value()));
          IFEM::cout << (*load.p)(0.0) << std::endl;
        }
        else
          load.p = utl::parseTimeFunc(child->FirstChild()->Value(),type);

        myLoads.push_back(load);
      }
    }

    else if (!myProblem->parse(child))
      ok = this->SIM1D::parse(child);
  }

  return ok;
}


/*!
  The twist angle is used to define the local element axes of beam elements
  along the spline curves. The angle describes the rotation of the local
  Y-axis, relative to the globalized Y-axis of the beam.
*/

bool SIMElasticBar::parseTwist (const tinyxml2::XMLElement* elem)
{
  if (!strcasecmp(elem->Value(),"Zdirection"))
  {
    utl::getAttribute(elem,"x",XZp.x);
    utl::getAttribute(elem,"y",XZp.y);
    utl::getAttribute(elem,"z",XZp.z);
    IFEM::cout <<"\tZ-direction vector: "<< XZp;
  }
  else if (!strcasecmp(elem->Value(),"twist") && elem->FirstChild())
  {
    std::string type;
    utl::getAttribute(elem,"type",type);
    IFEM::cout <<"    Continuous twist angle:";
    if (!type.empty()) IFEM::cout <<" ("<< type <<")";
    twist = utl::parseRealFunc(elem->FirstChild()->Value(),type);
  }
  else
    return false;

  IFEM::cout << std::endl;
  return true;
}


bool SIMElasticBar::createFEMmodel (char)
{
  bool ok = true;
  ASMbase::resetNumbering();
  for (ASMbase* pch : myModel)
  {
    ok &= static_cast<ASMs1D*>(pch)->generateOrientedFEModel(XZp);
    if (twist && ok) static_cast<ASMs1D*>(pch)->applyTwist(*twist);
  }

  return ok;
}


bool SIMElasticBar::initMaterial (size_t propInd)
{
  ElasticBeam* beam = dynamic_cast<ElasticBeam*>(myProblem);
  if (beam && propInd < myBCSec.size())
    beam->setProperty(myBCSec[propInd]);

  return true;
}


bool SIMElasticBar::initNeumann (size_t propInd)
{
  ElasticCable* cable = dynamic_cast<ElasticCable*>(myProblem);
  if (!cable) return false;

  SclFuncMap::const_iterator sit = myScalars.find(propInd);

  if (sit != myScalars.end())
    cable->setMoment(sit->second,lcStiff);
  else
    return false;

  return true;
}


void SIMElasticBar::preprocessA ()
{
  if (nf == 3 && nsd < 3)
    nf = nsd; // 2D bar/cable, two DOFs per node

  this->printProblem();
  for (ASMbase* pch : myModel)
    pch->setNoFields(nf%10);
}


bool SIMElasticBar::preprocessB ()
{
  // Preprocess the nodal point loads, if any
  if (myLoads.empty())
    return true;

  int ipt = 0;
  bool ok = true;
  for (PointLoad& pl : myLoads)
    if (pl.xi >= 0.0 && pl.inod > 0)
    {
      int foundPoint = this->findLoadPoint(++ipt,pl.inod,pl.xi,pl.ldof < 0);
      if (foundPoint > 0)
        pl.inod = foundPoint;
      else if (foundPoint == 0)
        ok = false;
    }

  if (ipt > 0)
    IFEM::cout <<"\n"<< std::endl;

  return ok;
}


bool SIMElasticBar::renumberNodes (const std::map<int,int>& nodeMap)
{
  bool ok = this->SIM1D::renumberNodes(nodeMap);

  for (PointLoad& load : myLoads)
    if (load.xi < 0.0 && load.inod > 0)
      ok &= utl::renumber(load.inod,nodeMap,true);

  return ok;
}


bool SIMElasticBar::assembleDiscreteTerms (const IntegrandBase* itg,
                                           const TimeDomain& time)
{
  bool ok = true;
  if (itg != myProblem || !myEqSys)
    return ok;

  SystemVector* R = myEqSys->getVector(2); // External load vector
  double scl = 1.0;

  SIM::SolutionMode mode = itg->getMode();
  if (!myLoads.empty() && mode == SIM::ARCLEN)
    this->setMode(SIM::RHS_ONLY);

  if (!R || itg->getIntegrationPrm(4) != 1.0)
  {
    R = myEqSys->getVector(0); // System right-hand-side vector
    if (itg->getIntegrationPrm(3) <= 0.0) // HHT is used
      scl = itg->getIntegrationPrm(2) + 1.0; // alphaH + 1.0
  }

  if (R)
  {
    // Assemble external nodal point loads at current time step
    for (const PointLoad& load : myLoads)
    {
      double P = (*load.p)(time.t);
      int ldof = load.ldof;
      if (ldof > 0)
      {
        // This load is directly in a nodal point
        myEqSys->addScalar(P,ldof-1);
        ok &= mySam->assembleSystem(*R,P*scl,std::make_pair(load.inod,ldof));
      }
      else // This is an element point load
        ok &= this->assemblePoint(load.inod,load.xi,P,-ldof);
    }
  }

  if (mode == SIM::ARCLEN)
    R = myEqSys->getVector(1); // External load gradient for arc-length driver
  else
    R = nullptr;

  if (R)
  {
    // Assemble external nodal point load gradient at current time step
    this->setMode(mode);
    static_cast<ElasticBase*>(myProblem)->setLoadGradientMode();
    for (const PointLoad& load : myLoads)
    {
      double P = load.p->deriv(time.t);
      int ldof = load.ldof;
      if (ldof > 0) // This load is directly in a nodal point
        ok &= mySam->assembleSystem(*R,P,std::make_pair(load.inod,ldof));
      else // This is an element point load
        ok &= this->assemblePoint(load.inod,load.xi,P,-ldof);
    }
  }

  return ok;
}


bool SIMElasticBar::updateRotations (const RealArray& incSol, double alpha)
{
  if (nf != 6) return true;

  bool ok = true;
  for (ASMbase* pch : myModel)
    if (incSol.empty())
      // Update the rotations of last converged load/time step
      static_cast<ASMs1D*>(pch)->updateRotations();
    else
    {
      Vector locSol;
      pch->extractNodeVec(incSol,locSol);
      if (alpha != 0.0 && alpha != 1.0) locSol *= alpha;
      ok &= static_cast<ASMs1D*>(pch)->updateRotations(locSol,alpha==0.0);
    }

  return ok;
}


Tensor SIMElasticBar::getNodeRotation (int inod) const
{
  for (const ASMbase* pch : myModel)
  {
    size_t node = pch->getNodeIndex(inod,true);
    if (node > 0)
      return static_cast<const ASMs1D*>(pch)->getRotation(node);
  }

  return Tensor(nsd,true);
}


void SIMElasticBar::shiftGlobalNums (int nshift, int)
{
  for (PointLoad& load : myLoads)
    if (load.inod > 0 && load.ldof > 0)
      load.inod += nshift;
}
