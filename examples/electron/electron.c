/*
 * Solvated electron in superfluid helium.
 * The electron-helium pseudo potential requires
 * fairly good resolution to be evaluated correctly,
 * ca. 0.2 Bohr spatial step.
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

/* Initial guess for bubble radius */
#define BUBBLE_RADIUS 1.0

/* #define INCLUDE_VORTEX 1 /**/
#define INCLUDE_ELECTRON 1 /**/

REAL rho0;

REAL complex bubble(void *NA, REAL x, REAL y, REAL z) {

  if(SQRT(x*x + y*y + z*z) < BUBBLE_RADIUS) return 0.0;
  return SQRT(rho0);
}

int main(int argc, char *argv[]) {

  FILE *fp;
  INT l, nx, ny, nz, iterations, threads;
  INT itp = 0, dump_nth, model;
  REAL step, time_step, mu0, time_step_el;
  char chk[256];
  INT restart = 0;
  wf3d *gwf = 0;
  wf3d *gwfp = 0;
  wf3d *egwf = 0;
  wf3d *egwfp = 0;
  rgrid3d *density = 0, *temp = 0;
  rgrid3d *pseudo = 0;
  cgrid3d *potential_store = 0;

  /* parameters */
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <paramfile.dat>\n", argv[0]);
    return 1;
  }
  
  if(!(fp = fopen(argv[1], "r"))) {
    fprintf(stderr, "Unable to open %s\n", argv[1]);
    return 1;
  }

  if(fscanf(fp, " threads = " FMT_I "%*[^\n]", &threads) < 1) {
    fprintf(stderr, "Invalid number of threads.\n");
    exit(1);
  }

  if(fscanf(fp, " grid = " FMT_I " " FMT_I " " FMT_I "%*[^\n]", &nx, &ny, &nz) < 3) {
    fprintf(stderr, "Invalid grid dimensions.\n");
    exit(1);
  }
  fprintf(stderr, "Grid (" FMT_I "x" FMT_I "x" FMT_I ")\n", nx, ny, nz);

  if(fscanf(fp, " gstep = " FMT_R "%*[^\n]", &step) < 1) {
    fprintf(stderr, "Invalid grid step.\n");
    exit(1);
  }
  
  if(fscanf(fp, " timestep = " FMT_R "%*[^\n]", &time_step) < 1) {
    fprintf(stderr, "Invalid time step.\n");
    exit(1);
  }

  if(fscanf(fp, " timestep_el = " FMT_R "%*[^\n]", &time_step_el) < 1) {
    fprintf(stderr, "Invalid time step.\n");
    exit(1);
  }

  fprintf(stderr, "Liquid time step = " FMT_R " fs, electron time step = " FMT_R " fs.\n", time_step, time_step_el);
  
  if(fscanf(fp, " iter = " FMT_I "%*[^\n]", &iterations) < 1) {
    fprintf(stderr, "Invalid number of iterations.\n");
    exit(1);
  }

  if(fscanf(fp, " itermode = " FMT_I "%*[^\n]", &itp) < 1) {
    fprintf(stderr, "Invalid iteration mode (0 = real time, 1 = imaginary time).\n");
    exit(1);
  }

  if(fscanf(fp, " dump = " FMT_I "%*[^\n]", &dump_nth) < 1) {
    fprintf(stderr, "Invalid dump iteration specification.\n");
    exit(1);
  }
  if(fscanf(fp, " model = " FMT_I "%*[^\n]", &model) < 1) {
    fprintf(stderr, "Invalid model.\n");
    exit(1);
  }
  if(fscanf(fp, " rho0 = " FMT_R "%*[^\n]", &rho0) < 1) {
    fprintf(stderr, "Invalid density.\n");
    exit(1);
  }
  rho0 *= GRID_AUTOANG * GRID_AUTOANG * GRID_AUTOANG;
  fprintf(stderr,"rho0 = " FMT_R " a.u.\n", rho0);
  if(fscanf(fp, " restart = " FMT_I "%*[^\n]", &restart) < 1) {
    fprintf(stderr, "Invalid restart data.\n");
    exit(1);
  }
  printf("restart = " FMT_I ".\n", restart);
  fclose(fp);

#ifdef USE_CUDA
  cuda_enable(1);  // enable CUDA ?
#endif

  /* allocate memory (3 x grid dimension, */
  fprintf(stderr,"Model = " FMT_I ".\n", model);
  dft_driver_setup_grid(nx, ny, nz, step, threads);
  dft_driver_setup_model(model, itp, rho0);
  dft_driver_setup_boundary_type(DFT_DRIVER_BOUNDARY_REGULAR, 0.0, 0.0, 0.0, 0.0);
  dft_driver_setup_normalization(DFT_DRIVER_DONT_NORMALIZE, 0, 0.0, 0);
  /* Neumann boundaries */
  dft_driver_setup_boundary_condition(DFT_DRIVER_BC_NEUMANN);
  dft_driver_initialize();

  density = dft_driver_alloc_rgrid("density");
  pseudo = dft_driver_alloc_rgrid("pseudo");
  temp = dft_driver_alloc_rgrid("temp");
  potential_store = dft_driver_alloc_cgrid("potential_store");
  gwf = dft_driver_alloc_wavefunction(DFT_HELIUM_MASS, "gwf");
  gwfp = dft_driver_alloc_wavefunction(DFT_HELIUM_MASS, "gwfp");
  egwf = dft_driver_alloc_wavefunction(1.0, "egwf"); /* electron mass */
  egwf->norm = 1.0; /* one electron */
  egwfp = dft_driver_alloc_wavefunction(1.0, "egwfp"); /* electron mass */
  egwfp->norm = 1.0; /* one electron */

  /* initialize wavefunctions */
  dft_driver_gaussian_wavefunction(egwf, 0.0, 0.0, 0.0, 14.5);
  grid3d_wf_normalize(egwf);
  cgrid3d_map(gwf->grid, bubble, (void *) NULL);

  if(restart) {
    fprintf(stderr, "Restart calculation\n");
    dft_driver_read_density(density, "restart.chk");
    rgrid3d_power(density, density, 0.5);
    grid3d_real_to_complex_re(gwf->grid, density);
    cgrid3d_copy(gwfp->grid, gwf->grid);
    dft_driver_read_density(density, "el-restart.chk");
    rgrid3d_power(density, density, 0.5);
    grid3d_real_to_complex_re(egwf->grid, density);
    cgrid3d_copy(egwfp->grid, egwf->grid);
    l = 1;
  } else l = 0;

#ifdef INCLUDE_ELECTRON  
  fprintf(stderr,"Electron included.\n");
  dft_common_potential_map(DFT_DRIVER_AVERAGE_NONE, "jortner.dat", "jortner.dat", "jortner.dat", pseudo);
  dft_driver_convolution_prepare(pseudo, NULL);
#else
  rgrid3d_zero(pseudo);
#endif

  fprintf(stderr,"Specified rho0 = " FMT_R " Angs^-3\n", rho0);
  mu0 = dft_ot_bulk_chempot2(dft_driver_otf);
  fprintf(stderr,"mu0 = " FMT_R " K.\n", mu0 * GRID_AUTOK);
  fprintf(stderr,"Applied P = " FMT_R " MPa.\n", dft_ot_bulk_pressure(dft_driver_otf, rho0) * GRID_AUTOPA / 1E6);
  
  /* Include vortex line initial guess along Z */
#ifdef INCLUDE_VORTEX
  fprintf(stderr,"Vortex included.\n");
  dft_driver_vortex_initial(gwf, 1, DFT_DRIVER_VORTEX_Z);
#endif

  /* solve */
  for(; l < iterations; l++) {

    if(!(l % dump_nth) || l == iterations-1 || l == 1) {
      REAL energy, natoms;
      energy = dft_driver_energy(gwf, NULL);
#ifdef INCLUDE_ELECTRON      
      energy += dft_driver_kinetic_energy(egwf); /* Liquid E + impurity kinetic E */
      grid3d_wf_density(gwf, density);
      dft_driver_convolution_prepare(density, NULL);
      dft_driver_convolution_eval(temp, density, pseudo);  // px is temp here
      
      grid3d_wf_density(egwf, density);
      rgrid3d_product(density, density, temp);
      energy += rgrid3d_integral(density);      /* Liquid - impurity interaction energy */
#endif      
      natoms = dft_driver_natoms(gwf);
      fprintf(stderr,"Energy with respect to bulk = " FMT_R " K.\n", (energy - dft_ot_bulk_energy(dft_driver_otf, rho0) * natoms / rho0) * GRID_AUTOK);
      fprintf(stderr,"Number of He atoms = " FMT_R ".\n", natoms);
      fprintf(stderr,"mu0 = %le K, energy/natoms = " FMT_R " K\n", mu0 * GRID_AUTOK,  GRID_AUTOK * energy / natoms);

      /* Dump helium density */
      grid3d_wf_density(gwf, density);
      sprintf(chk, "helium-" FMT_I, l);
      dft_driver_write_density(density, chk);
#ifdef INCLUDE_ELECTRON
      /* Dump electron density */
      sprintf(chk, "el-" FMT_I, l);
      grid3d_wf_density(egwf, density);
      dft_driver_write_density(density, chk);
      /* Dump electron wavefunction */
      sprintf(chk, "el-wf-" FMT_I, l);
      dft_driver_write_grid(egwf->grid, chk);
#endif
      /* Dump helium wavefunction */
      sprintf(chk, "helium-wf-" FMT_I, l);
      dft_driver_write_grid(gwf->grid, chk);
    }

#ifdef INCLUDE_ELECTRON
    /***** Electron *****/
    grid3d_wf_density(gwf, density);
    dft_driver_convolution_prepare(density, NULL);
    dft_driver_convolution_eval(density, density, pseudo);
    /* It is OK to run just one step - in imaginary time but not in real time. */
    dft_driver_propagate_predict(DFT_DRIVER_PROPAGATE_OTHER, density /* ..potential.. */, egwf, egwfp, potential_store, time_step_el, l);
    dft_driver_propagate_correct(DFT_DRIVER_PROPAGATE_OTHER, density /* ..potential.. */, egwf, egwfp, potential_store, time_step_el, l);
#else
    cgrid3d_zero(egwf->grid);
#endif

    /***** Helium *****/
#ifdef INCLUDE_ELECTRON
    grid3d_wf_density(egwf, density);
    dft_driver_convolution_prepare(density, NULL);
    dft_driver_convolution_eval(density, density, pseudo);
#else
    rgrid3d_zero(density);
#endif
    rgrid3d_add(density, -mu0);
    dft_driver_propagate_predict(DFT_DRIVER_PROPAGATE_HELIUM, density /* ..potential.. */, gwf, gwfp, potential_store, time_step, l);
    dft_driver_propagate_correct(DFT_DRIVER_PROPAGATE_HELIUM, density /* ..potential.. */, gwf, gwfp, potential_store, time_step, l);
  }
  return 0;
}