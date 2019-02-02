
/*
 * Dynamics of a bubble (formed by external potential) travelling at
 * constant velocity in liquid helium (moving background). 
 * 
 */

#include "bubble.h"

extern void do_ke(dft_ot_functional *, wf *, INT);

REAL round_veloc(REAL veloc) {   // Round to fit the simulation box

  INT n;
  REAL v;

  n = (INT) (0.5 + (NX * STEP * HELIUM_MASS * veloc) / (HBAR * 2.0 * M_PI));
  v = ((REAL) n) * HBAR * 2.0 * M_PI / (NX * STEP * HELIUM_MASS);
//  printf("Requested velocity = %le m/s.\n", veloc * GRID_AUTOMPS);
//  printf("Nearest velocity compatible with PBC = %le m/s.\n", v * GRID_AUTOMPS);
  return v;
}

REAL momentum(REAL vx) {

  return HELIUM_MASS * vx / HBAR;
}

/* -I * cabs(tstep) = full imag time, cabs(tstep) = full real time */
REAL complex tstep(REAL complex tstep, INT iter) {
 
  REAL x = ((REAL) iter) / (REAL) STARTING_ITER;

  return (-I * (1.0 - x) + x) * CABS(tstep);  // not called with x > 1
}

/* -I * cabs(tstep) = full imag time, cabs(tstep) = full real time */
REAL complex tstep2(REAL complex tstep, INT iter) {
 
  return CABS(tstep);
//  return CABS(tstep) * (1.0 - 3E-2 * I);  // introduce small imag part
}

int main(int argc, char *argv[]) {

  dft_ot_functional *otf;
  wf *gwf;
#ifdef PC
  wf *gwfp;
#endif
  cgrid *cworkspace;
  rgrid *ext_pot;
#ifdef OUTPUT_GRID
  char filename[2048];
#endif
  REAL prev_vz = 0.0, vz, mu0, kz, rho0;
  INT iter;
  extern void analyze(dft_ot_functional *, wf *, INT, REAL);
  extern REAL pot_func(void *, REAL, REAL, REAL);
  grid_timer timer;
  
#ifdef USE_CUDA
  cuda_enable(1);
#endif

  /* Initialize threads & use wisdom */
  grid_set_fftw_flags(1);    // FFTW_MEASURE
  grid_threads_init(THREADS);
  grid_fft_read_wisdom(NULL);

  /* Allocate wave functions */
  if(!(gwf = grid_wf_alloc(NX, NY, NZ, STEP, DFT_HELIUM_MASS, WF_PERIODIC_BOUNDARY, PROPAGATOR, "gwf"))) {
    fprintf(stderr, "Cannot allocate gwf.\n");
    exit(1);
  }
#ifdef PC
  gwfp = grid_wf_clone(gwf, "gwfp");
#endif
  cworkspace = cgrid_clone(gwf->grid, "cworkspace");

  /* Allocate OT functional */
  if(!(otf = dft_ot_alloc(FUNCTIONAL, gwf, DFT_MIN_SUBSTEPS, DFT_MAX_SUBSTEPS))) {
    fprintf(stderr, "Cannot allocate otf.\n");
    exit(1);
  }
  rho0 = dft_ot_bulk_density_pressurized(otf, PRESSURE);
  mu0 = dft_ot_bulk_chempot_pressurized(otf, PRESSURE);
  printf("mu0 = " FMT_R " K/atom, rho0 = " FMT_R " Angs^-3.\n", mu0 * GRID_AUTOK, rho0 / (GRID_AUTOANG * GRID_AUTOANG * GRID_AUTOANG));
  // Velocity cutoff
  otf->veloc_cutoff = MAXVELOC;

  ext_pot = rgrid_clone(otf->density, "ext_pot");                /* allocate real external potential grid */

  printf("Potential: RMIN = " FMT_R ", RADD = " FMT_R ", A0 = " FMT_R ", A1 = " FMT_R ", A2 = " FMT_R ", A3 = " FMT_R ", A4 = " FMT_R ", A5 = " FMT_R "\n", RMIN, RADD, A0, A1, A2, A3, A4, A5);

  // If doing momving background with imaginary time, add the following contribution to avoid normalization
  // TODO: Is this in the manual?
  // TODO: rotation must have the corresponding term...
//  mu0 = mu0 + (HBAR * HBAR / (2.0 * gwf->mass)) * kz * kz;

  printf("Time step in fs   = " FMT_R "\n", TIME_STEP * GRID_AUTOFS);
  printf("Time step in a.u. = " FMT_R "\n", TIME_STEP);

  rgrid_smooth_map(ext_pot, pot_func, NULL, 2); /* External potential */

  if(argc == 1) {
    grid_wf_constant(gwf, SQRT(rho0));
    /* Mixed Imaginary & Real time iterations */
    printf("Warm up iterations.\n");

    for(iter = 0; iter < STARTING_ITER; iter++) {

      if(iter == 5) grid_fft_write_wisdom(NULL);

      grid_timer_start(&timer);

#ifdef PC
      /* Predict-Correct */
      grid_real_to_complex_re(cworkspace, ext_pot);
      dft_ot_potential(otf, cworkspace, gwf);
      cgrid_add(cworkspace, -mu0);
      grid_wf_propagate_predict(gwf, gwfp, cworkspace, -I * TIME_STEP);
      grid_add_real_to_complex_re(cworkspace, ext_pot);
      dft_ot_potential(otf, cworkspace, gwfp);
      cgrid_add(cworkspace, -mu0);
      cgrid_multiply(cworkspace, 0.5);  // Use (current + future) / 2
      grid_wf_propagate_correct(gwf, cworkspace, -I * TIME_STEP);
      // Chemical potential included - no need to normalize
#else
      grid_real_to_complex_re(cworkspace, ext_pot);
      dft_ot_potential(otf, cworkspace, gwf);
      cgrid_add(cworkspace, -mu0);
      grid_wf_propagate(gwf, cworkspace, -I * TIME_STEP);
#endif
      printf("Iteration " FMT_I " - Wall clock time = " FMT_R " seconds.\n", iter, grid_timer_wall_clock_time(&timer));
    }
    iter = 0;
  } else { /* restart from a file (.grd) */
    sscanf(argv[1], "bubble-" FMT_I ".grd", &iter);
    printf("Continuing from checkpoint file %s at iteration " FMT_I ".\n", argv[1], iter);
    cgrid_read_grid(gwf->grid, argv[1]);
  }

  /* If using Crank-Nicolson, apply absorbing boundary */
#if PROPAGATOR == WF_2ND_ORDER_CN
  gwf->ts_func = &grid_wf_absorb;
  gwf->abs_data.amp = ABS_AMP;  // Absorption amplitude  
  gwf->abs_data.data[0] = (INT) (ABS_WIDTH_X / STEP);  // lower x defining the boundary
  gwf->abs_data.data[1] = NX - (INT) (ABS_WIDTH_X / STEP); // upper x defining the boundary
  gwf->abs_data.data[2] = (INT) (ABS_WIDTH_Y / STEP);  // lower y defining the boundary
  gwf->abs_data.data[3] = NY - (INT) (ABS_WIDTH_Y / STEP); // upper y defining the boundary
  gwf->abs_data.data[4] = (INT) (ABS_WIDTH_Z / STEP);  // lower z defining the boundary
  gwf->abs_data.data[5] = NZ - (INT) (ABS_WIDTH_Z / STEP); // upper z defining the boundary
#ifdef PC
  gwfp->ts_func = gwf->ts_func;
  gwfp->abs_data.amp = gwf->abs_data.amp;
  gwfp->abs_data.data[0] = gwf->abs_data.data[0];
  gwfp->abs_data.data[1] = gwf->abs_data.data[1];
  gwfp->abs_data.data[2] = gwf->abs_data.data[2];
  gwfp->abs_data.data[3] = gwf->abs_data.data[3];
  gwfp->abs_data.data[4] = gwf->abs_data.data[4];
  gwfp->abs_data.data[5] = gwf->abs_data.data[5];
#endif
#endif

  /* Real time iterations */
  printf("Real time propagation.\n");

  for( ; iter < MAXITER; iter++) {

    // Increase background velocity slowly
    vz = round_veloc(AZ * TIME_STEP * (REAL) iter);     /* Round velocity to fit the spatial grid */
    if(vz < MAXVZ && vz != prev_vz) {
      printf("Current velocity = " FMT_R " m/s.\n", vz * GRID_AUTOMPS);
      kz = momentum(vz);
      cgrid_set_momentum(gwf->grid, 0.0, 0.0, kz);
#ifdef PC
      cgrid_set_momentum(gwfp->grid, 0.0, 0.0, kz);
      cgrid_set_momentum(cworkspace, 0.0, 0.0, kz);
#endif
      prev_vz = vz;
    }
#ifdef OUTPUT_GRID
    if(!(iter % OUTPUT_GRID)) {
      sprintf(filename, "liquid-" FMT_I, iter);
      cgrid_write_grid(filename, gwf->grid);
      do_ke(otf, gwf, iter);
    }
#endif
    if(!(iter % OUTPUT_ITER)) analyze(otf, gwf, iter, vz);
//    grid_timer_start(&timer);
#ifdef PC
    /* Predict-Correct */
    grid_real_to_complex_re(cworkspace, ext_pot);
    dft_ot_potential(otf, cworkspace, gwf);
    cgrid_add(cworkspace, -mu0);
    grid_wf_propagate_predict(gwf, gwfp, cworkspace, TIME_STEP);
    grid_add_real_to_complex_re(cworkspace, ext_pot);
    dft_ot_potential(otf, cworkspace, gwfp);
    cgrid_add(cworkspace, -mu0);
    cgrid_multiply(cworkspace, 0.5);  // Use (current + future) / 2
    grid_wf_propagate_correct(gwf, cworkspace, TIME_STEP);
    // Chemical potential included - no need to normalize
#else
    grid_real_to_complex_re(cworkspace, ext_pot);
    dft_ot_potential(otf, cworkspace, gwf);
    cgrid_add(cworkspace, -mu0);
    grid_wf_propagate(gwf, cworkspace, TIME_STEP);
#endif
//    printf("Iteration " FMT_I " - Wall clock time = " FMT_R " seconds.\n", iter, grid_timer_wall_clock_time(&timer));
  }

  return 0;
}
