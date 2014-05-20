/*
 * Impurity atom in superfluid helium (no zero-point).
 * Interaction of impurity with vortex line.
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

#define TIME_STEP 20.0 /* fs */
#define MAXITER 10000000
#define NX 128
#define NY 128
#define NZ 128
#define STEP 0.5

#define HE2STAR 1 /**/
/* #define HESTAR  1 /**/

/* #define ONSAGER /**/

/*#define IMPURITY   /* Just the impurity */
#define VORTEX     /* Just the vortex */
/* #define BOTH       /* Both on top of each other */

#define HELIUM_MASS (4.002602 / GRID_AUTOAMU)
#define HBAR 1.0        /* au */

void zero_core(cgrid3d *grid) {

  long i, j, k;
  long nx = grid->nx, ny = grid->ny, nz = grid->nz;
  double x, y, step = grid->step;
  double complex *val = grid->value;
  
  for (i = 0; i < nx; i++)
    for (j = 0; j < ny; j++)
      for (k = 0; k < nz; k++) {
	x = (i - nx/2) * step;
	y = (j - ny/2) * step;
	if(sqrt(x * x + y * y) < step/2.0)
	  val[i * ny * nz + j * nz + k] = 0.0;
      }
}

int main(int argc, char **argv) {

  cgrid3d *potential_store;
  rgrid3d *ext_pot, *density, *px, *py, *pz;
  wf3d *gwf, *gwfp;
  long iter, N;
  double energy, natoms, mu0, rho0, width;

  /* Setup DFT driver parameters (256 x 256 x 256 grid) */
  dft_driver_setup_grid(NX, NY, NZ, STEP /* Bohr */, 32 /* threads */);
  /* Plain Orsay-Trento in imaginary time */
  dft_driver_setup_model(DFT_OT_PLAIN + DFT_OT_KC + DFT_OT_BACKFLOW, DFT_DRIVER_IMAG_TIME, 0.0);
  /* No absorbing boundary */
  dft_driver_setup_boundaries(DFT_DRIVER_BOUNDARY_REGULAR, 2.0);
  /* Neumann boundaries */
  dft_driver_setup_boundary_condition(DFT_DRIVER_BC_NEUMANN);

  /* Normalization condition */
  if(argc != 2) {
    fprintf(stderr, "Usage: imag_time N\n");
    exit(1);
  }
  N = atoi(argv[1]);
  if(N == 0) 
    dft_driver_setup_normalization(DFT_DRIVER_DONT_NORMALIZE, 0, 0.0, 1); // 1 = release center immediately
  else
    dft_driver_setup_normalization(DFT_DRIVER_NORMALIZE_DROPLET, N, 0.0, 1); // 1 = release center immediately

  printf("N = %ld\n", N);

  /* Initialize the DFT driver */
  dft_driver_initialize();

  /* Allocate space for external potential */
  ext_pot = dft_driver_alloc_rgrid();
  potential_store = dft_driver_alloc_cgrid(); /* temporary storage */
  density = dft_driver_alloc_rgrid();
  px = dft_driver_alloc_rgrid();
  py = dft_driver_alloc_rgrid();
  pz = dft_driver_alloc_rgrid();

  /* Allocate space for wavefunctions (initialized to sqrt(rho0)) */
  gwf = dft_driver_alloc_wavefunction(HELIUM_MASS); /* helium wavefunction */
  gwfp = dft_driver_alloc_wavefunction(HELIUM_MASS);/* temp. wavefunction */

#ifdef HE2STAR
  dft_common_potential_map(DFT_DRIVER_AVERAGE_NONE, "he2-He.dat-spline", "he2-He.dat-spline", "he2-He.dat-spline", ext_pot);
#endif
#ifdef HESTAR
  dft_common_potential_map(DFT_DRIVER_AVERAGE_NONE, "He-star-He.dat", "He-star-He.dat", "He-star-He.dat", ext_pot);
#endif
  //  rgrid3d_shift(ext_pot, density, 0.0, 0.0, 0.0);
#ifdef VORTEX
  rgrid3d_zero(ext_pot);
#endif

#ifdef ONSAGER
#if defined(VORTEX) || defined(BOTH)
  dft_driver_vortex(ext_pot, DFT_DRIVER_VORTEX_X);
#endif
#endif

  mu0 = bulk_chempot(dft_driver_otf);
  rgrid3d_add(ext_pot, -mu0);
  rho0 = bulk_density(dft_driver_otf);

  if(N != 0) {
    width = 1.0 / 20.0;
    cgrid3d_map(gwf->grid, dft_common_cgaussian, (void *) &width);
  } else cgrid3d_constant(gwf->grid, sqrt(rho0));

#ifndef ONSAGER
#if defined(VORTEX) || defined(BOTH)
  dft_driver_vortex_initial(gwf, 1, DFT_DRIVER_VORTEX_Z);
#endif
#endif

  for (iter = 1; iter < MAXITER; iter++) {
    
    if(iter == 1 || !(iter % 100)) {
      char buf[512];
      grid3d_wf_density(gwf, density);
      sprintf(buf, "output-%ld", iter);
      dft_driver_write_density(density, buf);
      energy = dft_driver_energy(gwf, ext_pot);
      natoms = dft_driver_natoms(gwf);
      printf("Total energy is %le K\n", energy * GRID_AUTOK);
      printf("Number of He atoms is %le.\n", natoms);
      printf("Energy / atom is %le K\n", (energy/natoms) * GRID_AUTOK);
#if 0
      grid3d_wf_probability_flux(gwf, px, py, pz);
      sprintf(buf, "flux_x-%ld", iter);
      dft_driver_write_density(px, buf);
      sprintf(buf, "flux_y-%ld", iter);
      dft_driver_write_density(py, buf);
      sprintf(buf, "flux_z-%ld", iter);
      dft_driver_write_density(pz, buf);
#endif
#if 0
      { long k;
	dft_driver_veloc_field(gwf, px, py, pz);
	grid3d_wf_density(gwf, density);
	for (k = 0; k < px->nx * px->ny * px->nz; k++)
	  px->value[k] = px->value[k] * px->value[k] + py->value[k] * py->value[k] + pz->value[k] * pz->value[k];
	rgrid3d_product(px, px, density);
	rgrid3d_multiply(px, 0.5);
	sprintf(buf, "kinetic-%ld", iter);
	dft_driver_write_density(px, buf);
      }
#endif
    }
    dft_driver_propagate_predict(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, gwf, gwfp, potential_store, TIME_STEP, iter);
    dft_driver_propagate_correct(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, gwf, gwfp, potential_store, TIME_STEP, iter);

#if defined(VORTEX) || defined(BOTH)
    zero_core(gwf->grid);
#endif
  }
  return 0;
}
