#ifndef PHG4VPrototype2CryostatSteppingAction_h
#define PHG4VPrototype2CryostatSteppingAction_h

#include <g4main/PHG4SteppingAction.h>

class PHG4Prototype2CryostatDetector;
class PHG4Parameters;
class PHG4Hit;
class PHG4HitContainer;

class PHG4Prototype2CryostatSteppingAction : public PHG4SteppingAction
{

  public:

  //! constructor
  PHG4Prototype2CryostatSteppingAction( PHG4Prototype2CryostatDetector*, PHG4Parameters *parameters );

  //! destroctor
  virtual ~PHG4Prototype2CryostatSteppingAction()
  {}

  //! stepping action
  virtual bool UserSteppingAction(const G4Step*, bool);

  //! reimplemented from base class
  virtual void SetInterfacePointers( PHCompositeNode* );

  double GetLightCorrection(const double r) const;

  private:

  //! pointer to the detector
  PHG4Prototype2CryostatDetector* detector_;

  //! pointer to hit container
  PHG4HitContainer * hits_;
  PHG4HitContainer * absorberhits_;
  PHG4Hit *hit;
  PHG4Parameters *params;
  // since getting parameters is a map search we do not want to
  // do this in every step, the parameters used are cached
  // in the following variables
  int absorbertruth;
  int IsActive;
  int IsBlackHole;
  int light_scint_model;
  
  double light_balance_inner_corr;
  double light_balance_inner_radius;
  double light_balance_outer_corr;
  double light_balance_outer_radius;
};


#endif // PHG4Prototype2CryostatSteppingAction_h
