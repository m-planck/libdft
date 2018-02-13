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

double rho0;

/* vortex ring initial guess */
double complex vring(void *asd, double x, double y, double z) {

  double xs = sqrt(x * x + y * y) - RING_RADIUS;
  double ys = z;
  double angle = atan2(ys,xs), r = sqrt(xs*xs + ys*ys);

//  return sqrt(rho0);
  // was -r^2 / 2.0. -r gives better vortex density profile
  return (1.0 - exp(-r)) * sqrt(rho0) * cexp(I * angle);
}

int main(int argc, char **argv) {

  rgrid3d *ext_pot;
  cgrid3d *potential_store;
  wf3d *gwf, *gwfp;
  long iter;
  double mu0, kin, pot, n;
  char buf[512];

  /* Setup DFT driver parameters (grid) */
  dft_driver_setup_grid(NX, NY, NZ, STEP, THREADS);
  /* Plain Orsay-Trento in imaginary time */
  dft_driver_setup_model(DFT_OT_PLAIN, DFT_DRIVER_IMAG_TIME, 0.0);
  /* No absorbing boundary */
  dft_driver_setup_boundary_type(DFT_DRIVER_BOUNDARY_REGULAR, 0.0, 0.0, 0.0, 0.0);
  /* Normalization condition */
  dft_driver_setup_normalization(DFT_DRIVER_DONT_NORMALIZE, 0, 3.0, 10);

  /* Initialize the DFT driver */
  dft_driver_initialize();

  /* density */
  rho0 = dft_ot_bulk_density_pressurized(dft_driver_otf, PRESSURE);
  dft_driver_otf->rho0 = rho0;
  /* chemical potential */
  mu0 = dft_ot_bulk_chempot_pressurized(dft_driver_otf, PRESSURE);
  printf("rho0 = %le Angs^-3, mu0 = %le K.\n", rho0 / (GRID_AUTOANG * GRID_AUTOANG * GRID_AUTOANG), mu0 * GRID_AUTOK);

  /* Allocate space for external potential */
  ext_pot = dft_driver_alloc_rgrid();
  potential_store = dft_driver_alloc_cgrid(); /* temporary storage */

  /* Allocate space for wavefunctions (initialized to sqrt(rho0)) */
  gwf = dft_driver_alloc_wavefunction(HELIUM_MASS); /* helium wavefunction */
  gwfp = dft_driver_alloc_wavefunction(HELIUM_MASS);/* temp. wavefunction */
  /* setup initial guess for vortex ring */
  cgrid3d_map(gwf->grid, vring, NULL);

  /* Generate the excited potential */
  rgrid3d_constant(ext_pot, -mu0); /* Add the chemical potential */

  for (iter = 1; iter < 800000; iter++) {
    if(iter == 1 || !(iter % NTH)) {
      sprintf(buf, "vring-%ld", iter);
      dft_driver_write_grid(gwf->grid, buf);
      dft_driver_propagate_predict(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, gwf, gwfp, potential_store, TS, iter);
      dft_driver_propagate_correct(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, gwf, gwfp, potential_store, TS, iter);
      kin = dft_driver_kinetic_energy(gwf);            /* Kinetic energy for gwf */
      pot = dft_driver_potential_energy(gwf, ext_pot); /* Potential energy for gwf */
      n = dft_driver_natoms(gwf);
      printf("Iteration %ld helium natoms    = %le particles.\n", iter, n);   /* Energy / particle in K */
      printf("Iteration %ld helium kinetic   = %.30lf\n", iter, kin * GRID_AUTOK);  /* Print result in K */
      printf("Iteration %ld helium potential = %.30lf\n", iter, pot * GRID_AUTOK);  /* Print result in K */
      printf("Iteration %ld helium energy    = %.30lf\n", iter, (kin + pot) * GRID_AUTOK);  /* Print result in K */
      fflush(stdout);
    }
  }
  return 0;
}
