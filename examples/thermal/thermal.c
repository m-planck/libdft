/*
 * Impurity atom in superfluid helium (no zero-point).
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

#define NX 256
#define NY 256
#define NZ 256
#define STEP 0.5
#define TS 1.0

#define PRESSURE 0.0

#define MAXITER 10000000
#define NTH 10

#define THREADS 0

#define CUTOFF 1.5
REAL complex high_cut(void *NA, REAL kx, REAL ky, REAL kz) {

  REAL d = SQRT(kx*kx + ky*ky + kz*kz);

  if(d > CUTOFF) return 0.0;
  else return 1.0;
}

int main(int argc, char **argv) {

  dft_ot_functional *otf;
  cgrid *potential_store;
  rgrid *density;
  wf *gwf, *gwfp;
  INT iter;
  REAL energy, natoms, mu0, rho0;
  grid_timer timer;

#ifdef USE_CUDA
  cuda_enable(1);
#endif

  /* Initialize threads & use wisdom */
  grid_set_fftw_flags(1);    // FFTW_MEASURE
  grid_threads_init(THREADS);
  grid_fft_read_wisdom(NULL);

  /* Allocate wave functions */
  if(!(gwf = grid_wf_alloc(NX, NY, NZ, STEP, DFT_HELIUM_MASS, WF_PERIODIC_BOUNDARY, WF_2ND_ORDER_FFT, "gwf"))) {
    fprintf(stderr, "Cannot allocate gwf.\n");
    exit(1);
  }
  gwfp = grid_wf_clone(gwf, "gwfp");

  /* Allocate OT functional */
  if(!(otf = dft_ot_alloc(DFT_OT_PLAIN, gwf, DFT_MIN_SUBSTEPS, DFT_MAX_SUBSTEPS))) {
    fprintf(stderr, "Cannot allocate otf.\n");
    exit(1);
  }
  rho0 = dft_ot_bulk_density_pressurized(otf, PRESSURE);
  mu0 = dft_ot_bulk_chempot_pressurized(otf, PRESSURE);
  printf("mu0 = " FMT_R " K/atom, rho0 = " FMT_R " Angs^-3.\n", mu0 * GRID_AUTOK, rho0 / (GRID_AUTOANG * GRID_AUTOANG * GRID_AUTOANG));

  /* Allocate space for external potential */
  potential_store = cgrid_clone(gwf->grid, "potential_store");
  density = rgrid_clone(otf->density, "density");

  grid_wf_constant(gwf, SQRT(rho0));
  cgrid_random(gwf->grid, 5E-2); // 9e-3
  cgrid_fft(gwf->grid);
  cgrid_fft_filter(gwf->grid, &high_cut, NULL);
  cgrid_inverse_fft_norm(gwf->grid);

  /* Run 200 iterations using imaginary time (10 fs time step) */
  REAL temp, itime = 0.0, qp, qp_kc, cl_bf;

  gwf->norm = grid_wf_norm(gwf);
  for (iter = 0; iter < MAXITER; iter++) {

#define TEMP 1.0

//    grid_wf_density(gwf, otf->density);
//    rgrid_zero(density);
//    dft_ot_energy_density_bf(otf, density, gwf, otf->density);    
//    cl_bf = rgrid_integral(density);    
    cl_bf = 0.0;
    temp = grid_wf_ideal_gas_temperature(gwf, cl_bf, otf->workspace1, otf->workspace2);

    if(temp - TEMP < 0.0) itime = -1E-4;
    if(temp - TEMP > 0.0) itime = 1E-4;

    if(!(iter % NTH)) {
//      grid_wf_density(gwf, otf->density);
//      rgrid_zero(density);
//      dft_ot_energy_density_kc(otf, density, gwf, otf->density);
//      qp_kc = rgrid_integral(density);
      qp_kc = 0.0;
      qp = grid_wf_kinetic_energy_qp(gwf, otf->workspace1, otf->workspace2);
            
      dft_ot_energy_density(otf, density, gwf);
      printf("Total E/FFT  = " FMT_R " K.\n", grid_wf_energy_fft(gwf, density) * GRID_AUTOK);
      printf("Total E/CN   = " FMT_R " K.\n", grid_wf_energy_cn(gwf, density) * GRID_AUTOK);
      printf("QP energy    = " FMT_R " K.\n", (qp + qp_kc) * GRID_AUTOK);
      printf("Classical E. = " FMT_R " K.\n", (grid_wf_kinetic_energy_cn(gwf) - qp + cl_bf) * GRID_AUTOK);
      printf("Itime        = " FMT_R " fs.\n", itime);
      printf("T            = " FMT_R " K.\n", temp);
      printf("Circulation  = " FMT_R ".\n", grid_wf_circulation(gwf, 1.0, otf->density, otf->workspace1, otf->workspace2, otf->workspace3));
      fflush(stdout);
    }

    if(iter == 5) grid_fft_write_wisdom(NULL);

//    grid_timer_start(&timer);

    /* Predict-Correct */
    cgrid_zero(potential_store);
    dft_ot_potential(otf, potential_store, gwf);
    cgrid_add(potential_store, -mu0);
    grid_wf_propagate_predict(gwf, gwfp, potential_store, (-I * itime + TS) / GRID_AUTOFS);
    dft_ot_potential(otf, potential_store, gwfp);
    cgrid_add(potential_store, -mu0);
    cgrid_multiply(potential_store, 0.5);  // Use (current + future) / 2
    grid_wf_propagate_correct(gwf, potential_store, (-I * itime + TS) / GRID_AUTOFS);

//    cgrid_zero(potential_store);
//    dft_ot_potential(otf, potential_store, gwf);
//    cgrid_add(potential_store, -mu0);
//    grid_wf_propagate(gwf, potential_store, (-I * itime * TS + TS) / GRID_AUTOFS);

//    printf("Iteration " FMT_I " - Wall clock time = " FMT_R " seconds.\n", iter, grid_timer_wall_clock_time(&timer));

    if(!(iter % (500*NTH))) {
      char buf[512];
      sprintf(buf, "output-" FMT_I, iter);
      grid_wf_density(gwf, density);
      rgrid_write_grid(buf, density);
      sprintf(buf, "wf-output-" FMT_I, iter);
      cgrid_write_grid(buf, gwf->grid);
      dft_ot_energy_density(otf, density, gwf);
      energy = grid_wf_energy(gwf, NULL) + rgrid_integral(density);
      natoms = grid_wf_norm(gwf);
//      printf("Total energy is " FMT_R " K\n", energy * GRID_AUTOK);
//      printf("Number of He atoms is " FMT_R ".\n", natoms);
//      printf("Energy / atom is " FMT_R " K\n", (energy/natoms) * GRID_AUTOK);
//      fflush(stdout);
    }
  }
  /* At this point gwf contains the converged wavefunction */
  grid_wf_density(gwf, density);
  rgrid_write_grid("output", density);
  return 0;
}
