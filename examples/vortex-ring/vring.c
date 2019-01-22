/*
 * Create a vortex ring in superfluid helium centered around Z = 0.
 *
 * All input in a.u. except the time step, which is fs.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <grid/grid.h>
#include <grid/au.h>
#include <dft/dft.h>
#include <dft/ot.h>

#define TS 30.0 /* fs */
#define NX 256
#define NY 256
#define NZ 256
#define STEP 1.0
#define NTH 1000
#define THREADS 0

#define RING_RADIUS 40.0

#define PRESSURE (0.0 / GRID_AUTOBAR)

#define HELIUM_MASS (4.002602 / GRID_AUTOAMU)

REAL rho0;

/* vortex ring initial guess */
REAL complex vring(void *asd, REAL x, REAL y, REAL z) {

  REAL xs = SQRT(x * x + y * y) - RING_RADIUS;
  REAL ys = z;
  REAL angle = ATAN2(ys,xs), r = SQRT(xs*xs + ys*ys);

//  return SQRT(rho0);
  // was -r^2 / 2.0. -r gives better vortex density profile
  return (1.0 - EXP(-r)) * SQRT(rho0) * CEXP(I * angle);
}

int main(int argc, char **argv) {

  cgrid *potential_store;
  rgrid *rworkspace;
  wf *gwf, *gwfp;
  INT iter;
  REAL mu0, kin, pot, n;
  char buf[512];

#ifdef USE_CUDA
  cuda_enable(1);  // enable CUDA ?
#endif

  /* Setup DFT driver parameters (grid) */
  dft_driver_setup_grid(NX, NY, NZ, STEP, THREADS);
  /* Plain Orsay-Trento in imaginary time */
  dft_driver_setup_model(DFT_OT_PLAIN|DFT_OT_KC|DFT_OT_BACKFLOW, DFT_DRIVER_IMAG_TIME, 0.0);
  /* No absorbing boundary */
  dft_driver_setup_boundary_type(DFT_DRIVER_BOUNDARY_REGULAR, 0.0, 0.0, 0.0, 0.0);
  /* Normalization condition */
  dft_driver_setup_normalization(DFT_DRIVER_DONT_NORMALIZE, 0, 3.0, 10);

  /* Allocate space for wavefunctions (initialized to SQRT(rho0)) */
  gwf = dft_driver_alloc_wavefunction(HELIUM_MASS, "gwf"); /* helium wavefunction */
  gwfp = dft_driver_alloc_wavefunction(HELIUM_MASS, "gwfp");/* temp. wavefunction */

  /* Initialize the DFT driver */
  dft_driver_initialize(gwf);

  /* density */
  rho0 = dft_ot_bulk_density_pressurized(dft_driver_otf, PRESSURE);
  dft_driver_otf->rho0 = rho0;
  /* chemical potential */
  mu0 = dft_ot_bulk_chempot_pressurized(dft_driver_otf, PRESSURE);
  printf("rho0 = " FMT_R " Angs^-3, mu0 = " FMT_R " K.\n", rho0 / (GRID_AUTOANG * GRID_AUTOANG * GRID_AUTOANG), mu0 * GRID_AUTOK);

  /* Allocate space for external potential */
  potential_store = dft_driver_alloc_cgrid("potential_store"); /* temporary storage */
  rworkspace = dft_driver_alloc_rgrid("rworkspace"); /* temporary storage */
 
  /* setup initial guess for vortex ring */
  cgrid_map(gwf->grid, vring, NULL);

  for (iter = 1; iter < 800000; iter++) {
    if(iter == 1 || !(iter % NTH)) {
      sprintf(buf, "vring-" FMT_I, iter);
      cgrid_write_grid(buf, gwf->grid);
      dft_driver_propagate_predict(DFT_DRIVER_PROPAGATE_HELIUM, NULL, mu0, gwf, gwfp, potential_store, TS, iter);
      dft_driver_propagate_correct(DFT_DRIVER_PROPAGATE_HELIUM, NULL, mu0, gwf, gwfp, potential_store, TS, iter);
      kin = grid_wf_energy(gwf, NULL);            /* Kinetic energy for gwf */
      dft_ot_energy_density(dft_driver_otf, rworkspace, gwf);
      pot = rgrid_integral(rworkspace);
      n = grid_wf_norm(gwf);
      printf("Iteration " FMT_I " helium natoms    = " FMT_R " particles.\n", iter, n);   /* Energy / particle in K */
      printf("Iteration " FMT_I " helium kinetic   = " FMT_R "\n", iter, kin * GRID_AUTOK);  /* Print result in K */
      printf("Iteration " FMT_I " helium potential = " FMT_R "\n", iter, pot * GRID_AUTOK);  /* Print result in K */
      printf("Iteration " FMT_I " helium energy    = " FMT_R "\n", iter, (kin + pot) * GRID_AUTOK);  /* Print result in K */
      fflush(stdout);
    }
  }
  return 0;
}
