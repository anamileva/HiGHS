/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                       */
/*    This file is part of the HiGHS linear optimization suite           */
/*                                                                       */
/*    Written and engineered 2008-2021 at the University of Edinburgh    */
/*                                                                       */
/*    Available as open-source under the MIT License                     */
/*                                                                       */
/*    Authors: Julian Hall, Ivet Galabova, Qi Huangfu, Leona Gottwald    */
/*    and Michael Feldmeier                                              */
/*                                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**@file simplex/HSimplexNla.h
 *
 * @brief Interface to HFactor allowing non-HFactor updates, NLA-only
 * scaling and shifting of NLA analysis below simplex level.
 */
#ifndef HSIMPLEXNLA_H_
#define HSIMPLEXNLA_H_

#include "lp_data/HStruct.h"
#include "simplex/HFactor.h"
#include "simplex/HMatrix.h"
#include "simplex/HighsSimplexAnalysis.h"

//#include <algorithm>

// using std::max;
// using std::min;
// using std::vector;
//
// class HVector;

class HSimplexNla {
 public:
  void setup(const HighsLp* lp, HighsInt* base_index,
             const double factor_pivot_threshold, const HighsOptions* options,
             HighsTimer* timer, HighsSimplexAnalysis* analysis);
  HighsInt invert();
  void btran(HVector& rhs, const double expected_density,
             HighsTimerClock* factor_timer_clock_pointer = NULL) const;
  void ftran(HVector& rhs, const double expected_density,
             HighsTimerClock* factor_timer_clock_pointer = NULL) const;
  void update(HVector* aq, HVector* ep, HighsInt* iRow, HighsInt* hint);
  void setPivotThreshold(const double new_pivot_threshold);
  void passScaleAndFactorMatrixPointers(const HighsScale* scale,
                                        const HighsInt* factor_a_start,
                                        const HighsInt* factor_a_index,
                                        const double* factor_a_value);
  void applyBasisMatrixColScale(HVector& rhs, const HighsScale* scale) const;
  void applyBasisMatrixRowScale(HVector& rhs, const HighsScale* scale) const;
  void undoBasisMatrixColScale(HVector& rhs, const HighsScale* scale) const;
  void undoBasisMatrixRowScale(HVector& rhs, const HighsScale* scale) const;
  bool sparseLoopStyle(const HighsInt count, const HighsInt dim,
                       HighsInt& to_entry) const;
  HighsInt build_synthetic_tick_;

  // private:
  // References:
  //
  // Pointers:

  // Class data members
  const HighsLp* lp_;
  const HighsScale* scale_;
  HighsInt* base_index_;
  const HighsOptions* options_;
  HighsTimer* timer_;
  HighsSimplexAnalysis* analysis_;

  //  HMatrix matrix_;
  HFactor factor_;
};

#endif /* HSIMPLEXNLA_H_ */
