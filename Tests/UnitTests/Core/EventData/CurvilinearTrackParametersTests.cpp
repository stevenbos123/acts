// This file is part of the Acts project.
//
// Copyright (C) 2017-2020 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <boost/test/unit_test.hpp>

#include "Acts/Definitions/Algebra.hpp"
#include "Acts/Definitions/Common.hpp"
#include "Acts/Definitions/TrackParametrization.hpp"
#include "Acts/Definitions/Units.hpp"
#include "Acts/EventData/Charge.hpp"
#include "Acts/EventData/GenericCurvilinearTrackParameters.hpp"
#include "Acts/EventData/TrackParameters.hpp"
#include "Acts/Geometry/GeometryContext.hpp"
#include "Acts/Tests/CommonHelpers/FloatComparisons.hpp"
#include "Acts/Utilities/UnitVectors.hpp"
#include "Acts/Utilities/detail/periodic.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "TrackParametersDatasets.hpp"

namespace {

using namespace Acts;
using namespace Acts::UnitLiterals;
using AnyCurvilinearTrackParameters =
    GenericCurvilinearTrackParameters<AnyCharge>;

constexpr auto eps = 8 * std::numeric_limits<ActsScalar>::epsilon();
const GeometryContext geoCtx;
const BoundSquareMatrix cov = BoundSquareMatrix::Identity();

template <typename charge_t>
void checkParameters(const GenericCurvilinearTrackParameters<charge_t>& params,
                     double phi, double theta, double p, double q,
                     const Vector4& pos4, const Vector3& unitDir) {
  const auto qOverP = (q != 0) ? (q / p) : (1 / p);
  const auto pos = pos4.segment<3>(ePos0);

  // native values
  CHECK_SMALL(params.template get<eBoundLoc0>(), eps);
  CHECK_SMALL(params.template get<eBoundLoc1>(), eps);
  CHECK_CLOSE_OR_SMALL(params.template get<eBoundTime>(), pos4[eTime], eps,
                       eps);
  CHECK_CLOSE_OR_SMALL(detail::radian_sym(params.template get<eBoundPhi>()),
                       detail::radian_sym(phi), eps, eps);
  CHECK_CLOSE_OR_SMALL(params.template get<eBoundTheta>(), theta, eps, eps);
  CHECK_CLOSE_OR_SMALL(params.template get<eBoundQOverP>(), qOverP, eps, eps);
  // convenience accessorss
  CHECK_CLOSE_OR_SMALL(params.fourPosition(geoCtx), pos4, eps, eps);
  CHECK_CLOSE_OR_SMALL(params.position(geoCtx), pos, eps, eps);
  CHECK_CLOSE_OR_SMALL(params.time(), pos4[eTime], eps, eps);
  CHECK_CLOSE_OR_SMALL(params.direction(), unitDir, eps, eps);
  CHECK_CLOSE_OR_SMALL(params.absoluteMomentum(), p, eps, eps);
  CHECK_CLOSE_OR_SMALL(params.transverseMomentum(), p * std::sin(theta), eps,
                       eps);
  CHECK_CLOSE_OR_SMALL(params.momentum(), p * unitDir, eps, eps);
  BOOST_CHECK_EQUAL(params.charge(), q);
  // curvilinear reference surface
  CHECK_CLOSE_OR_SMALL(params.referenceSurface().center(geoCtx), pos, eps, eps);
  CHECK_CLOSE_OR_SMALL(params.referenceSurface().normal(geoCtx), unitDir, eps,
                       eps);
  // TODO verify reference frame
}

}  // namespace

BOOST_AUTO_TEST_SUITE(EventDataCurvilinearTrackParameters)

BOOST_DATA_TEST_CASE(
    NeutralConstruct,
    posSymmetric* posSymmetric* posSymmetric* ts* phis* thetas* ps, x, y, z,
    time, phiInput, theta, p) {
  // phi is ill-defined in forward/backward tracks
  const auto phi = ((0 < theta) and (theta < M_PI)) ? phiInput : 0.0;
  const Vector4 pos4(x, y, z, time);
  const Vector3 dir = makeDirectionFromPhiTheta(phi, theta);

  NeutralCurvilinearTrackParameters params(pos4, dir, 1 / p);
  checkParameters(params, phi, theta, p, 0_e, pos4, dir);
  BOOST_CHECK(not params.covariance());

  // reassign w/ covariance
  params = NeutralCurvilinearTrackParameters(pos4, dir, 1 / p, cov);
  BOOST_CHECK(params.covariance());
  BOOST_CHECK_EQUAL(params.covariance().value(), cov);
}

BOOST_DATA_TEST_CASE(
    ChargedConstruct,
    posSymmetric* posSymmetric* posSymmetric* ts* phis* thetas* ps* qsNonZero,
    x, y, z, time, phiInput, theta, p, q) {
  // phi is ill-defined in forward/backward tracks
  const auto phi = ((0 < theta) and (theta < M_PI)) ? phiInput : 0.0;
  const Vector4 pos4(x, y, z, time);
  const Vector3 dir = makeDirectionFromPhiTheta(phi, theta);

  CurvilinearTrackParameters params(pos4, dir, q / p);
  checkParameters(params, phi, theta, p, q, pos4, dir);
  BOOST_CHECK(not params.covariance());

  // reassign w/ covariance
  params = CurvilinearTrackParameters(pos4, dir, q / p, cov);
  BOOST_CHECK(params.covariance());
  BOOST_CHECK_EQUAL(params.covariance().value(), cov);
}

BOOST_DATA_TEST_CASE(
    AnyConstruct,
    posSymmetric* posSymmetric* posSymmetric* ts* phis* thetas* ps* qsAny, x, y,
    z, time, phiInput, theta, p, q) {
  // phi is ill-defined in forward/backward tracks
  const auto phi = ((0 < theta) and (theta < M_PI)) ? phiInput : 0.0;
  const Vector4 pos4(x, y, z, time);
  const Vector3 dir = makeDirectionFromPhiTheta(phi, theta);

  AnyCurvilinearTrackParameters params(pos4, dir, p, q);
  checkParameters(params, phi, theta, p, q, pos4, dir);
  BOOST_CHECK(not params.covariance());

  // reassign w/ covariance
  params = AnyCurvilinearTrackParameters(pos4, dir, p, q, cov);
  BOOST_CHECK(params.covariance());
  BOOST_CHECK_EQUAL(params.covariance().value(), cov);
}

BOOST_AUTO_TEST_SUITE_END()
