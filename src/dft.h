/*
 * Headers for generic DFT routines.
 *
 */

/*
 * Defines 
 *
 */

#define DFT_MAX_POTENTIAL_POINTS 8192

/*
 * Structures
 *
 */

typedef struct lj_struct {
  double h, sigma, epsilon;
} dft_common_lj;

typedef struct extpot {
  double points[DFT_MAX_POTENTIAL_POINTS];
  double begin;
  long length;
  double step;
} dft_extpot;

typedef struct extpot_set {
  dft_extpot *x;
  dft_extpot *y;
  dft_extpot *z;
  int average;
} dft_extpot_set;

typedef struct extpot_set_2d {
  dft_extpot *z;
  dft_extpot *r;
  int average;
} dft_extpot_set_2d;

/*
 * Prototypes (auto generated).
 *
 */

#define EXPORT
