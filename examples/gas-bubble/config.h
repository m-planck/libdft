/*
 * All input in a.u. except the time step, which is in fs.
 *
 */

#define TIME_STEP 15.0                  /* Time step in imag/real iterations (fs) */
#define FUNCTIONAL (DFT_OT_PLAIN)       /* Functional to be used (could add DFT_OT_KC and/or DFT_OT_BACKFLOW) */
#define STARTING_TIME 400000.0          /* Start real time simulation at this time (fs) - 10 ps (was 400,000) */
#define STARTING_ITER ((INT) (STARTING_TIME / TIME_STEP))
#define MAXITER 4                /* Maximum number of real time iterations */
#define OUTPUT_TIME 2500.0              /* Output interval time (fs) (2500) */
#define OUTPUT_ITER ((INT) (OUTPUT_TIME / TIME_STEP))
/* #define OUTPUT_GRID                  /* Output grid at each iteration (takes lots of space) (leave undefined if not needed) */
#define CUDA                            /* Use CUDA ? */

#define VX (60.0 / GRID_AUTOMPS)        /* Flow velocity (m/s) */
#define PRESSURE (0.0 / GRID_AUTOBAR)   /* External pressure in bar (normal = 0) */

#define THREADS 0	/* # of parallel threads to use (0 = all) */
#define NX 512       	/* # of grid points along x */
#define NY 256          /* # of grid points along y */
#define NZ 256        	/* # of grid points along z */
#define STEP 2.0        /* spatial step length (Bohr) */
#define ABS_AMP 2.0     /* Absorption strength */
#define ABS_WIDTH_X 60.0  /* Width of the absorbing boundary */
#define ABS_WIDTH_Y 25.0  /* Width of the absorbing boundary */
#define ABS_WIDTH_Z 25.0  /* Width of the absorbing boundary */

#define KINETIC_PROPAGATOR DFT_DRIVER_KINETIC_FFT      /* FFT (unstable) */
/* #define KINETIC_PROPAGATOR DFT_DRIVER_KINETIC_CN_NBC /* Crank-Nicolson (stable, but slower) */

#define FFTW_PLANNER 1 /* 0: FFTW_ESTIMATE, 1: FFTW_MEASURE (default), 2: FFTW_PATIENT, 3: FFTW_EXHAUSTIVE */

#define NN 2.0    /* Exponent for circulation (1, 2, 3...) */

/* Bubble parameters using exponential repulsion (approx. electron bubble) - RADD = 19.0 */
#define A0 (3.8003E5 / GRID_AUTOK)
#define A1 (1.6245 * GRID_AUTOANG)
#define A2 0.0
#define A3 0.0
#define A4 0.0
#define A5 0.0
#define RMIN 2.0
#define RADD 6.0