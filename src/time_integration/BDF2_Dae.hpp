#ifndef _BDF2_DAE_HPP_
#define _BDF2_DAE_HPP_

#include "Epetra_Vector.h"
#include "Epetra_BlockMap.h"

#include "BDF2_State.hpp"


namespace BDF2 {

  class Dae {

  public:

    Dae(Epetra_BlockMap& map, int mitr, double ntol, int mvec, double vtol); 
    Dae(Epetra_BlockMap& map);

    void set_initial_state(const double t, const Epetra_Vector& x, const Epetra_Vector& xdot);

    void commit_solution(const double h, const Epetra_Vector& u);
    void select_step_size(const std::vector<double>& dt, const double perr, double& h); 

    void trap_step_one(double h, Epetra_Vector& u, double& hnext, int& errc);
    void bdf2_step_gen(double h, Epetra_Vector& u, double& hnext, int& errc, bool ectrl);
    void bdf2_step_simple(double h, Epetra_Vector& u, double& hnext, int& errc, bool ectrl);

    void bdf2_step(double& h, double hmin, int mtries, Epetra_Vector& u, double& hnext); 


  private:

    double rmin; 
    double rmax; 
    double margin;

    State state;

    // constants
    const static double RMIN = 0.25;
    const static double RMAX = 4.0;
    const static double MARGIN = 3.0;

    // successful STATUS return codes
    const static int SOLVED_TO_TOUT = 1;
    const static int SOLVED_TO_NSTEP = 2;
    
    // unsuccessful STATUS return codes
    const static int BAD_INPUT = -1;
    const static int STEP_FAILED = -2;
    const static int STEP_SIZE_TOO_SMALL = -3;

  };

}

#endif // _BDF2_DAE_HPP_
