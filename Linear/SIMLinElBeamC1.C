// $Id$
//==============================================================================
//!
//! \file SIMLinElBeamC1.C
//!
//! \date Sep 16 2010
//!
//! \author Knut Morten Okstad / SINTEF
//!
//! \brief Solution driver for NURBS-based FE analysis of C1-continous beams.
//!
//==============================================================================

#include "SIMLinElBeamC1.h"
#include "KirchhoffLovePlate.h"
#include "ElasticityUtils.h"
#include "LinIsotropic.h"
#include "AnalyticSolutions.h"
#include "AlgEqSystem.h"
#include "SAM.h"
#include "Functions.h"
#include "Utilities.h"
#include "Property.h"
#include "AnaSol.h"
#include "Vec3Oper.h"
#include "IFEM.h"
#include "tinyxml2.h"


SIMLinElBeamC1::~SIMLinElBeamC1 ()
{
  for (Material* mat : mVec)
    delete mat;
}


void SIMLinElBeamC1::clearProperties ()
{
  KirchhoffLove* klp = dynamic_cast<KirchhoffLove*>(myProblem);
  if (klp)
  {
    klp->setMaterial(nullptr);
    klp->setPressure(nullptr);
  }

  for (Material* mat : mVec)
    delete mat;

  tVec.clear();
  mVec.clear();
  myLoads.clear();

  this->SIM1D::clearProperties();
}


bool SIMLinElBeamC1::parse (char* keyWord, std::istream& is)
{
  if (!myProblem)
    myProblem = new KirchhoffLovePlate(1,1);

  char* cline;
  KirchhoffLovePlate* klp = dynamic_cast<KirchhoffLovePlate*>(myProblem);
  if (!klp) return false;

  if (!strncasecmp(keyWord,"GRAVITY",7))
  {
    double g = atof(strtok(keyWord+7," "));
    klp->setGravity(g);
    IFEM::cout <<"\nGravitation constant: "<< g << std::endl;
  }

  else if (!strncasecmp(keyWord,"ISOTROPIC",9))
  {
    int nmat = atoi(keyWord+10);
    IFEM::cout <<"\nNumber of isotropic materials: "<< nmat << std::endl;

    for (int i = 0; i < nmat && (cline = utl::readLine(is)); i++)
    {
      int code = atoi(strtok(cline," "));
      if (code > 0)
        this->setPropertyType(code,Property::MATERIAL,mVec.size());

      double E   = atof(strtok(NULL," "));
      double rho = (cline = strtok(NULL, " ")) ? atof(cline) : 0.0;
      double thk = (cline = strtok(NULL, " ")) ? atof(cline) : 0.0;
      mVec.push_back(new LinIsotropic(E,0.0,rho,true));
      tVec.push_back(thk);
      IFEM::cout <<"\tMaterial code "<< code <<": "
                 << E <<" "<< rho <<" "<< thk << std::endl;
    }

    if (!mVec.empty())
      klp->setMaterial(mVec.front());
    if (!tVec.empty() && tVec.front() != 0.0)
      klp->setThickness(tVec.front());
  }

  else if (!strncasecmp(keyWord,"POINTLOAD",9))
  {
    int nload = atoi(keyWord+9);
    IFEM::cout <<"\nNumber of point loads: "<< nload;

    myLoads.resize(nload);
    for (int i = 0; i < nload && (cline = utl::readLine(is)); i++)
    {
      myLoads[i].patch = atoi(strtok(cline," "));
      myLoads[i].xi    = atof(strtok(NULL," "));
      myLoads[i].pload = atof(strtok(NULL," "));
      IFEM::cout <<"\n\tPoint "<< i+1 <<": P"<< myLoads[i].patch
                 <<" xi = "<< myLoads[i].xi <<" load = "<< myLoads[i].pload;
    }
  }

  else if (!strncasecmp(keyWord,"PRESSURE",8))
  {
    int npres = atoi(keyWord+8);
    IFEM::cout <<"\nNumber of pressures: "<< npres << std::endl;

    for (int i = 0; i < npres && (cline = utl::readLine(is)); i++)
    {
      int code = atoi(strtok(cline," "));
      double p = atof(strtok(NULL," "));
      IFEM::cout <<"\tPressure code "<< code <<": ";
      cline = strtok(NULL," ");
      myScalars[code] = const_cast<RealFunc*>(utl::parseRealFunc(cline,p));
      IFEM::cout << std::endl;
      if (code > 0)
        this->setPropertyType(code,Property::BODYLOAD);
    }
  }

  else if (!strncasecmp(keyWord,"ANASOL",6))
  {
    cline = strtok(keyWord+6," ");
    if (!strncasecmp(cline,"EXPRESSION",10))
    {
      IFEM::cout <<"\nAnalytical solution: Expression"<< std::endl;
      int lines = (cline = strtok(NULL," ")) ? atoi(cline) : 0;
      if (!mySol)
        mySol = new AnaSol(is,lines,false);
    }
    else
    {
      std::cerr <<"  ** SIMLinElBeamC1::parse: Unknown analytical solution "
                << cline <<" (ignored)"<< std::endl;
      return true;
    }
  }

  else
    return this->SIM1D::parse(keyWord,is);

  return true;
}


bool SIMLinElBeamC1::parse (const tinyxml2::XMLElement* elem)
{
  if (strcasecmp(elem->Value(),"eulerbernoulli"))
    return this->SIM1D::parse(elem);

  KirchhoffLovePlate* klp = dynamic_cast<KirchhoffLovePlate*>(myProblem);
  if (!klp)
  {
    int version = 1;
    utl::getAttribute(elem,"version",version);
    myProblem = klp = new KirchhoffLovePlate(1,version);
  }

  bool ok = true;
  const tinyxml2::XMLElement* child = elem->FirstChildElement();
  for (; child && ok; child = child->NextSiblingElement())

    if (!strcasecmp(child->Value(),"gravity"))
    {
      double g = 0.0;
      utl::getAttribute(child,"g",g);
      klp->setGravity(g);
      IFEM::cout <<"\tGravitation constant: "<< g << std::endl;
    }

    else if (!strcasecmp(child->Value(),"isotropic"))
    {
      int code = this->parseMaterialSet(child,mVec.size());

      double E = 1000.0, rho = 1.0, thk = 0.1;
      utl::getAttribute(child,"E",E);
      utl::getAttribute(child,"rho",rho);
      utl::getAttribute(child,"thickness",thk);

      mVec.push_back(new LinIsotropic(E,0.0,rho,true));
      tVec.push_back(thk);
      IFEM::cout <<"\tMaterial code "<< code <<": "
                 << E <<" "<< rho <<" "<< thk << std::endl;
      klp->setMaterial(mVec.front());
      if (tVec.front() != 0.0)
        klp->setThickness(tVec.front());
    }

    else if (!strcasecmp(child->Value(),"pointload") && child->FirstChild())
    {
      myLoads.resize(myLoads.size()+1);
      PointLoad& load = myLoads.back();
      bool allowElementPointLoad = false;
      load.pload = atof(child->FirstChild()->Value());
      if (utl::getAttribute(child,"patch",load.patch))
        IFEM::cout <<"\tPoint: P"<< load.patch;
      else
        IFEM::cout <<"\tPoint:";
      utl::getAttribute(child,"xi",load.xi);
      utl::getAttribute(child,"onElement",allowElementPointLoad);
      IFEM::cout <<" xi = "<< load.xi <<" load = "<< load.pload << std::endl;
      if (allowElementPointLoad) load.inod = -1;
    }

    else if (!strcasecmp(child->Value(),"pressure") && child->FirstChild())
    {
      std::string set, type;
      utl::getAttribute(child,"set",set);
      int code = this->getUniquePropertyCode(set,1);
      if (code == 0) utl::getAttribute(child,"code",code);
      if (code > 0)
      {
        utl::getAttribute(child,"type",type,true);
        IFEM::cout <<"\tPressure code "<< code;
        if (!type.empty()) IFEM::cout <<" ("<< type <<")";
        myScalars[code] = utl::parseRealFunc(child->FirstChild()->Value(),type);
        this->setPropertyType(code,Property::BODYLOAD);
        IFEM::cout << std::endl;
      }
    }

    else if (!strcasecmp(child->Value(),"rigid"))
      ok &= this->parseRigid(child,this);

    else if (!strcasecmp(child->Value(),"anasol"))
    {
      std::string type;
      utl::getAttribute(child,"type",type,true);
      if (type == "expression")
      {
        IFEM::cout <<"\tAnalytical solution: Expression"<< std::endl;
        if (!mySol)
          mySol = new AnaSol(child);
      }
      else
        std::cerr <<"  ** SIMLinElBeamC1::parse: Unknown analytical solution "
                  << type <<" (ignored)"<< std::endl;
    }

    else if (!klp->parse(child))
      ok = this->SIM1D::parse(child);

  return ok;
}


bool SIMLinElBeamC1::parseXi (const tinyxml2::XMLElement* elem, RealArray& xi) const
{
  std::string type;
  utl::getAttribute(elem,"type",type,true);
  if (type == "midpoint-focus")
  {
    int levels = 0;
    utl::getAttribute(elem,"levels",levels);
    if (levels < 1) return false;

    double hmax = pow(2.0,-levels);
    double hmin = hmax*hmax;
    IFEM::cout <<"\thmax = "<< hmax <<" ("<< 1.0/hmax
               <<")\n\thmin = "<< hmin <<" ("<< 1.0/hmin <<")"<< std::endl;

    std::set<double> knots;
    for (double x = hmax; x < 1.0; x += hmax)
      knots.insert(x);

    for (double h = hmax*0.5; h > hmin; h *= 0.5)
    {
      knots.insert(0.5-h);
      knots.insert(0.5+h);
    }

    xi.insert(xi.end(),knots.begin(),knots.end());
    return true;
  }

  return false;
}


bool SIMLinElBeamC1::initMaterial (size_t propInd)
{
  if (propInd >= mVec.size()) propInd = mVec.size()-1;

  KirchhoffLovePlate* klp = dynamic_cast<KirchhoffLovePlate*>(myProblem);
  if (!klp) return false;

  klp->setMaterial(mVec[propInd]);
  if (tVec[propInd] != 0.0)
    klp->setThickness(tVec[propInd]);

  return true;
}


bool SIMLinElBeamC1::initBodyLoad (size_t patchInd)
{
  KirchhoffLovePlate* klp = dynamic_cast<KirchhoffLovePlate*>(myProblem);
  if (!klp) return false;

  klp->setPressure();
  SclFuncMap::const_iterator it = myScalars.find(0);
  if (it != myScalars.end()) klp->setPressure(it->second);

  for (const Property& prop : myProps)
    if (prop.pcode == Property::BODYLOAD && prop.patch == patchInd)
      if ((it = myScalars.find(prop.pindx)) != myScalars.end())
        if (it->second) klp->setPressure(it->second);

  return true;
}


bool SIMLinElBeamC1::preprocessB ()
{
  // Preprocess the nodal point loads, if any
  if (myLoads.empty())
    return true;

  int ipt = 0;
  bool ok = true;
  for (PointLoad& pl : myLoads)
    if (!(pl.inod = this->findLoadPoint(++ipt,pl.patch,pl.xi,pl.inod < 0)))
      ok = false;

  IFEM::cout <<"\n"<< std::endl;
  return ok;
}


bool SIMLinElBeamC1::assembleDiscreteTerms (const IntegrandBase*,
                                            const TimeDomain&)
{
  if (!myEqSys) return true; // silently ignore if no equation system defined

  SystemVector* b = myEqSys->getVector();
  if (!b) return false;

  bool ok = true;
  for (const PointLoad& load : myLoads)
    if (load.inod > 0)
      ok &= mySam->assembleSystem(*b,&load.pload,load.inod);
    else if (load.inod < 0) // This is an element point load
      ok &= this->assemblePoint(load.patch,load.xi,load.pload);

  return ok;
}


double SIMLinElBeamC1::externalEnergy (const Vectors& u,
                                       const TimeDomain& time) const
{
  double energy = this->SIM1D::externalEnergy(u,time);

  // External energy from the nodal point loads
  for (const PointLoad& load : myLoads)
    if (load.inod > 0)
      energy += load.pload * u.front()(load.inod);
    else if (load.inod < 0) // This is an element point load
      energy += load.pload*this->getSolution(u.front(),load.xi,0,load.patch)[0];

  return energy;
}


void SIMLinElBeamC1::shiftGlobalNums (int nshift, int)
{
  for (PointLoad& load : myLoads)
    if (load.inod > 0) load.inod += nshift;
}


void SIMLinElBeamC1::printNormGroup (const Vector& gNorm, const Vector& rNorm,
                                     const std::string& prjName) const
{
  Elastic::printNorms(gNorm,rNorm,prjName,this);
}


bool SIMLinElBeamC1::haveAnaSol () const
{
  if (!mySol) return false;

  KirchhoffLovePlate* klp = dynamic_cast<KirchhoffLovePlate*>(myProblem);
  if (!klp) return false;

  if (klp->getVersion() > 1)
    return mySol->getScalarSecSol() != nullptr;

  return mySol->getStressSol() != nullptr;
}
