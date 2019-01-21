/*
 * Create sudden liquid compression by a moving piston.
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

#define TS 5.0 /* fs */
#define NX 1024       /* simulation box size */
#define NY 256
#define NZ 256
#define STEP 1.0     /* spatial grid step */
#define MAXITER 80000 /* maximum iterations */
#define INITIAL 400   /* Initial imaginary iterations */
#define NTH 100      /* output every NTH real time iterations */
#define THREADS 0    /* 0 = use all cores */

#define PISTON_VELOC (230.0 / GRID_AUTOMPS)   /* m/s */
#define PISTON_DIST  20.0     /* Bohr */

#define PRESSURE (1.0 / GRID_AUTOBAR)

#define HELIUM_MASS (4.002602 / GRID_AUTOAMU)

/* Bubble parameters using exponential repulsion (approx. electron bubble) - RADD = 19.0 */
#define A0 (3.8003E5 / GRID_AUTOK)
#define A1 (1.6245 * GRID_AUTOANG)
#define A2 0.0
#define A3 0.0
#define A4 0.0
#define A5 0.0
#define RMIN 2.0
#define RADD (6.0 + piston_pos)

REAL piston_pos = 0.0;

REAL piston(REAL time) {

  return PISTON_VELOC * time;
}

/* Impurity must always be at the origin (dU/dx) */
REAL dpot_func(void *NA, REAL x, REAL y, REAL z) {

  REAL r, rp, r2, r3, r5, r7, r9, r11;

  rp = SQRT(x * x + y * y + z * z);
  r = rp - RADD;
  if(r < RMIN) return 0.0;

  r2 = r * r;
  r3 = r2 * r;
  r5 = r2 * r3;
  r7 = r5 * r2;
  r9 = r7 * r2;
  r11 = r9 * r2;
  
  return (x / rp) * (-A0 * A1 * EXP(-A1 * r) + 4.0 * A2 / r5 + 6.0 * A3 / r7 + 8.0 * A4 / r9 + 10.0 * A5 / r11);
}

REAL pot_func(void *NA, REAL x, REAL y, REAL z) {

  REAL r, r2, r4, r6, r8, r10;

  r = SQRT(x * x + y * y + z * z);
  r -= RADD;
  if(r < RMIN) r = RMIN;

  r2 = r * r;
  r4 = r2 * r2;
  r6 = r4 * r2;
  r8 = r6 * r2;
  r10 = r8 * r2;
  return A0 * EXP(-A1 * r) - A2 / r4 - A3 / r6 - A4 / r8 - A5 / r10;
}

int main(int argc, char **argv) {

  rgrid *ext_pot, *rworkspace;
  cgrid *potential_store;
  wf *gwf, *gwfp;
  INT iter;
  REAL mu0, rho0;
  char buf[512];

  /* Setup DFT driver parameters (grid) */
  dft_driver_setup_grid(NX, NY, NZ, STEP, THREADS);
  /* Plain Orsay-Trento in imaginary time */
  dft_driver_setup_model(DFT_OT_PLAIN, DFT_DRIVER_IMAG_TIME, 0.0);
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

  /* Allocate space for external potential */
  ext_pot = dft_driver_alloc_rgrid("ext_pot");
  rworkspace = dft_driver_alloc_rgrid("rworkspace");
  potential_store = dft_driver_alloc_cgrid("potential_store"); /* temporary storage */

  /* map potential */
  rgrid_map(ext_pot, pot_func, NULL);

  /* Imag time iterations */
  for (iter = 0; iter < INITIAL; iter++) {
    dft_driver_propagate_predict(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, mu0, gwf, gwfp, potential_store, 5.0 * TS, iter);
    dft_driver_propagate_correct(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, mu0, gwf, gwfp, potential_store, 5.0 * TS, iter);
  }

  /* Real time iterations */
  dft_driver_setup_model(DFT_OT_PLAIN, DFT_DRIVER_REAL_TIME, 0.0);
  for (iter = 0; iter < MAXITER; iter++) {
    dft_driver_propagate_predict(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, mu0, gwf, gwfp, potential_store, TS, iter);
    dft_driver_propagate_correct(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, mu0, gwf, gwfp, potential_store, TS, iter);
    /* Move potential */
    if(piston_pos < PISTON_DIST) {
      piston_pos = piston(((REAL) iter) * TS / GRID_AUTOFS);
      rgrid_map(ext_pot, pot_func, NULL);
      /* end move */
    }
    if(!(iter % NTH)) {
      sprintf(buf, "piston-" FMT_I, iter);
      grid_wf_density(gwf, rworkspace);
      rgrid_write_grid(buf, rworkspace);
    }
  }
  return 0;
}
