/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                       */
/*    This file is part of the HiGHS linear optimization suite           */
/*                                                                       */
/*    Written and engineered 2008-2019 at the University of Edinburgh    */
/*                                                                       */
/*    Available as open-source under the MIT License                     */
/*                                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**@file ipm/IpxWrapperEmpty.h
 * @brief
 * @author Julian Hall, Ivet Galabova, Qi Huangfu and Michael Feldmeier
 */
#ifndef INTERIOR_POINT_IPX_WRAPPER_EMPTY_H_
#define INTERIOR_POINT_IPX_WRAPPER_EMPTY_H_

#include "lp_data/HConst.h"
#include "lp_data/HighsLp.h"
#include "ipm/IpxStatus.h"

HighsStatus solveModelIpx(const HighsLp& lp, const HighsOptions& options,
			HighsBasis& highs_basis, HighsSolution& highs_solution,
			HighsModelStatus& unscaled_model_status,
			HighsSolutionParams& unscaled_solution_params) {
  unscaled_model_status = HighsModelStatus::NOTSET;
  return HighsStatus::Error;
}

#endif
