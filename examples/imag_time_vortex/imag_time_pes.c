/*
 * Impurity atom in superfluid helium (no zero-point).
 * Scan the impurity vortex distance.
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
#define MAXITER 5000   /* was 5000 */
#define NX 256
#define NY 128
#define NZ 128
#define STEP 1.0

#define IBEGIN 70.0
#define ISTEP 5.0
#define IEND 0.0

/* #define HE2STAR 1 */
#define HESTAR  1

/* #define ONSAGER */

#define HELIUM_MASS (4.002602 / GRID_AUTOAMU)
#define HBAR 1.0        /* au */

void zero_core(cgrid *grid) {

  INT i, j, k;
  INT nx = grid->nx, ny = grid->ny, nz = grid->nz;
  REAL x, y, step = grid->step;
  REAL complex *val = grid->value;
  
  for (i = 0; i < nx; i++)
    for (j = 0; j < ny; j++)
      for (k = 0; k < nz; k++) {
	x = ((REAL) (i - nx/2)) * step;
	y = ((REAL) (j - ny/2)) * step;
	if(SQRT(x * x + y * y) < step/2.0)
	  val[i * ny * nz + j * nz + k] = 0.0;
      }
}

int main(int argc, char **argv) {

  cgrid *potential_store;
  rgrid *ext_pot, *orig_pot, *density, *px, *py, *pz;
  wf *gwf, *gwfp;
  char buf[512];
  INT iter, N;
  REAL energy, natoms, mu0, rho0, width, R;

  /* Setup DFT driver parameters (256 x 256 x 256 grid) */
  dft_driver_setup_grid(NX, NY, NZ, STEP /* Bohr */, 32 /* threads */);
  /* Plain Orsay-Trento in imaginary time */
  dft_driver_setup_model(DFT_OT_PLAIN, DFT_DRIVER_IMAG_TIME, 0.0);
  /* No absorbing boundary */
  dft_driver_setup_boundary_type(DFT_DRIVER_BOUNDARY_REGULAR, 0.0, 0.0, 0.0, 0.0);
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

  printf("N = " FMT_I "\n", N);

  /* Allocate space for wavefunctions (initialized to SQRT(rho0)) */
  gwf = dft_driver_alloc_wavefunction(HELIUM_MASS, "gwf"); /* helium wavefunction */
  gwfp = dft_driver_alloc_wavefunction(HELIUM_MASS, "gwfp");/* temp. wavefunction */

  /* Initialize the DFT driver */
  dft_driver_initialize(gwf);

  /* Allocate space for external potential */
  ext_pot = dft_driver_alloc_rgrid("ext_pot");
  orig_pot = dft_driver_alloc_rgrid("orig_pot");
  potential_store = dft_driver_alloc_cgrid("potential_store"); /* temporary storage */
  density = dft_driver_alloc_rgrid("density");
  px = dft_driver_alloc_rgrid("px");
  py = dft_driver_alloc_rgrid("py");
  pz = dft_driver_alloc_rgrid("pz");

#ifdef HE2STAR
  dft_common_potential_map(DFT_DRIVER_AVERAGE_NONE, "he2-He.dat-spline", "he2-He.dat-spline", "he2-He.dat-spline", orig_pot);
#endif
#ifdef HESTAR
  dft_common_potential_map(DFT_DRIVER_AVERAGE_NONE, "He-star-He.dat", "He-star-He.dat", "He-star-He.dat", orig_pot);
#endif
  mu0 = dft_ot_bulk_chempot(dft_driver_otf);
  rho0 = dft_ot_bulk_density(dft_driver_otf);

  if(N != 0) {
    width = 1.0 / 20.0;
    cgrid_map(gwf->grid, dft_common_cgaussian, (void *) &width);
  } else cgrid_constant(gwf->grid, SQRT(rho0));

#ifndef ONSAGER
  dft_driver_vortex_initial(gwf, 1, DFT_DRIVER_VORTEX_Z);
#endif

  for (R = IBEGIN; R >= IEND; R -= ISTEP) {
    rgrid_shift(ext_pot, orig_pot, R, 0.0, 0.0);
#ifdef ONSAGER
    dft_driver_vortex(ext_pot, DFT_DRIVER_VORTEX_X);
#endif
    // TODO: Do we need to enforce the vortex solution at each R?

    for (iter = 1; iter < ((R == IBEGIN)?10*MAXITER:MAXITER); iter++) {      
      dft_driver_propagate_predict(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, mu0, gwf, gwfp, potential_store, TIME_STEP, iter);
      dft_driver_propagate_correct(DFT_DRIVER_PROPAGATE_HELIUM, ext_pot, mu0, gwf, gwfp, potential_store, TIME_STEP, iter);
      zero_core(gwf->grid);
    }

    printf("Results for R = " FMT_R "\n", R);
    grid_wf_density(gwf, density);
    sprintf(buf, "output-" FMT_R, R);
    rgrid_write_grid(buf, density);
    dft_ot_energy_density(dft_driver_otf, density, gwf);
    rgrid_add_scaled_product(density, 1.0, dft_driver_otf->density, ext_pot);
    energy = grid_wf_energy(gwf, NULL) + rgrid_integral(density);
    natoms = grid_wf_norm(gwf);
    printf("Total energy is " FMT_R " K\n", energy * GRID_AUTOK);
    printf("Number of He atoms is " FMT_R ".\n", natoms);
    printf("Energy / atom is " FMT_R " K\n", (energy/natoms) * GRID_AUTOK);
    grid_wf_probability_flux(gwf, px, py, pz);
    sprintf(buf, "flux_x-" FMT_R, R);
    rgrid_write_grid(buf, px);
    sprintf(buf, "flux_y-" FMT_R, R);
    rgrid_write_grid(buf, py);
    sprintf(buf, "flux_z-" FMT_R, R);
    rgrid_write_grid(buf, pz);
    printf("PES " FMT_R " " FMT_R "\n", R, energy * GRID_AUTOK);
  }
  return 0;
}
