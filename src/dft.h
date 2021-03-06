/*
 * Headers for generic DFT routines.
 *
 */

#include "classical.h"

#ifndef __DFT_COMMON__
#define __DFT_COMMON__

/*
 * Preprocessor defines 
 *
 */

#define DFT_MAX_POTENTIAL_POINTS 8192

/*
 * Structures
 *
 */

/* Lennard-Jones potential parameters */
typedef struct lj_struct {
  REAL h;	/* Short range cutoff distance */
  REAL sigma;	/* Parameter sigma for Lennard-Jones */
  REAL epsilon; /* Parameter epsilon for Lennard-Jones */
  REAL cval;    /* Constant value when r < h */
} dft_common_lj;

/* Structure for holding external potential */
typedef struct extpot {
  REAL points[DFT_MAX_POTENTIAL_POINTS];   /* Array holding potential energy values */
  REAL begin;                              /* Starting distance for potential */
  INT length;                              /* Number of points in the potential array */
  REAL step;	                           /* Step length between potential points */
} dft_extpot;

/* Structure for holding external potential along the three Cartesian axes */
typedef struct extpot_set {
  dft_extpot *x;			   /* Potential along x-axis */
  dft_extpot *y;			   /* Potential along y-axis */
  dft_extpot *z;			   /* Potential along z-axis */
  char average;				   /* Averaging: 0 = no averaging */
                                           /* 1 = average in xy-plane */
                                           /* 2 = average in yz-plane */
                                           /* 3 = average in xz-plane */
                                           /* 4 = spherical average */
  REAL theta0;                             /* orientation of the potential */
  REAL phi0;                               /* (theta0, phi0) (rotation) */
  REAL x0, y0, z0;                         /* Origin for the potential */
} dft_extpot_set;

/* Structure defining a plane wave */
typedef struct {
  REAL kx, ky, kz;                         /* Wave vectors along x, y, z */
  REAL a;				   /* Relative amplitude */
                                           /* Absolute amp = a * sqrt(rho) */
  REAL rho;                                /* Background amplitude = sqrt(rho) */
} dft_plane_wave;

/*
 * Prototypes (auto generated by Makefile).
 *
 */

#define EXPORT

#endif
