// This file is part of the Acts project.
//
// Copyright (C) 2023 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "Acts/Surfaces/PerigeeSurface.hpp"
#include "Acts/Utilities/UnitVectors.hpp"
#include "Acts/Vertexing/LinearizerTrackParameters.hpp"

template <typename propagator_t, typename propagator_options_t>
Acts::Result<Acts::LinearizedTrack>
Acts::NumericalTrackLinearizer<propagator_t, propagator_options_t>::
    linearizeTrack(const BoundTrackParameters& params, const Vector4& linPoint,
                   const Acts::GeometryContext& gctx,
                   const Acts::MagneticFieldContext& mctx,
                   State& /*state*/) const {
  // Make Perigee surface at linPointPos, transverse plane of Perigee
  // corresponds the global x-y plane
  Vector3 linPointPos = VectorHelpers::position(linPoint);
  std::shared_ptr<PerigeeSurface> perigeeSurface =
      Surface::makeShared<PerigeeSurface>(linPointPos);

  // Create propagator options
  propagator_options_t pOptions(gctx, mctx);

  // Length scale at which we consider to be sufficiently close to the Perigee
  // surface to skip the propagation.
  pOptions.targetTolerance = m_cfg.targetTolerance;

  // Get intersection of the track with the Perigee if the particle would
  // move on a straight line.
  // This allows us to determine whether we need to propagate the track
  // forward or backward to arrive at the PCA.
  auto intersection = perigeeSurface->intersect(gctx, params.position(gctx),
                                                params.direction(), false);

  // Setting the propagation direction using the intersection length from
  // above.
  // We handle zero path length as forward propagation, but we could actually
  // skip the whole propagation in this case.
  pOptions.direction =
      Direction::fromScalarZeroAsPositive(intersection.intersection.pathLength);

  // Propagate to the PCA of linPointPos
  auto result = m_cfg.propagator->propagate(params, *perigeeSurface, pOptions);

  // Extracting the Perigee representation of the track wrt linPointPos
  auto endParams = *result->endParameters;
  BoundVector perigeeParams = endParams.parameters();

  // Covariance and weight matrix at the PCA to "linPoint"
  BoundSquareMatrix parCovarianceAtPCA = endParams.covariance().value();
  BoundSquareMatrix weightAtPCA = parCovarianceAtPCA.inverse();

  // Vector containing the track parameters at the PCA
  // Note that we parametrize the track using the following parameters:
  // (x, y, z, t, phi, theta, q/p),
  // where
  // -) (x, y, z, t) is the global 4D position of the PCA
  // -) phi and theta are the global angles of the momentum at the PCA
  // -) q/p is the charge divided by the total momentum at the PCA
  Acts::ActsVector<eLinSize> paramVec;

  // 4D PCA and the momentum of the track at the PCA
  // These quantities will be used in the computation of the constant term in
  // the Taylor expansion
  Vector4 pca;
  Vector3 momentumAtPCA;

  // Fill "paramVec", "pca", and "momentumAtPCA"
  {
    Vector3 globalCoords = endParams.position(gctx);
    ActsScalar globalTime = endParams.time();
    ActsScalar phi = perigeeParams(BoundIndices::eBoundPhi);
    ActsScalar theta = perigeeParams(BoundIndices::eBoundTheta);
    ActsScalar qOvP = perigeeParams(BoundIndices::eBoundQOverP);

    paramVec << globalCoords, globalTime, phi, theta, qOvP;
    pca << globalCoords, globalTime;
    momentumAtPCA << phi, theta, qOvP;
  }

  // Complete Jacobian (consists of positionJacobian and momentumJacobian)
  ActsMatrix<eBoundSize, eLinSize> completeJacobian =
      ActsMatrix<eBoundSize, eLinSize>::Zero(eBoundSize, eLinSize);

  // Perigee parameters wrt linPoint after wiggling
  BoundVector newPerigeeParams;

  // Check if wiggled angle theta are within definition range [0, pi]
  if (paramVec(eLinTheta) + m_cfg.delta > M_PI) {
    ACTS_ERROR(
        "Wiggled theta outside range, choose a smaller wiggle (i.e., delta)! "
        "You might need to decrease targetTolerance as well.")
  }

  // Wiggling each of the parameters at the PCA and computing the Perigee
  // parametrization of the resulting new track. This allows us to approximate
  // the numerical derivatives.
  for (unsigned int i = 0; i < eLinSize; i++) {
    Acts::ActsVector<eLinSize> paramVecCopy = paramVec;
    // Wiggle
    paramVecCopy(i) += m_cfg.delta;

    // Create curvilinear track object from our parameters. This is needed for
    // the propagation. Note that we work without covariance since we don't need
    // it to compute the derivative.
    Vector3 wiggledDir = makeDirectionFromPhiTheta(paramVecCopy(eLinPhi),
                                                   paramVecCopy(eLinTheta));
    // Since we work in 4D we have eLinPosSize = 4
    CurvilinearTrackParameters wiggledCurvilinearParams(
        paramVecCopy.head(eLinPosSize), wiggledDir, paramVecCopy(eLinQOverP));

    // Obtain propagation direction
    intersection = perigeeSurface->intersect(
        gctx, paramVecCopy.head(eLinPosSize - 1), wiggledDir, false);
    pOptions.direction = Direction::fromScalarZeroAsPositive(
        intersection.intersection.pathLength);

    // Propagate to the new PCA and extract Perigee parameters
    auto newResult = m_cfg.propagator->propagate(wiggledCurvilinearParams,
                                                 *perigeeSurface, pOptions);
    newPerigeeParams = (*newResult->endParameters).parameters();

    // Computing the numerical derivatives and filling the Jacobian
    completeJacobian.array().col(i) =
        (newPerigeeParams - perigeeParams) / m_cfg.delta;
    // We need to account for the periodicity of phi. We overwrite the
    // previously computed value for better readability.
    completeJacobian(eLinPhi, i) =
        Acts::detail::difference_periodic(newPerigeeParams(eLinPhi),
                                          perigeeParams(eLinPhi), 2 * M_PI) /
        m_cfg.delta;
  }

  // Extracting positionJacobian and momentumJacobian from the complete Jacobian
  ActsMatrix<eBoundSize, eLinPosSize> positionJacobian =
      completeJacobian.block<eBoundSize, eLinPosSize>(0, 0);
  ActsMatrix<eBoundSize, eLinMomSize> momentumJacobian =
      completeJacobian.block<eBoundSize, eLinMomSize>(0, eLinPosSize);

  // Constant term of Taylor expansion (Eq. 5.38 in Ref. (1))
  BoundVector constTerm =
      perigeeParams - positionJacobian * pca - momentumJacobian * momentumAtPCA;

  return LinearizedTrack(perigeeParams, parCovarianceAtPCA, weightAtPCA,
                         linPoint, positionJacobian, momentumJacobian, pca,
                         momentumAtPCA, constTerm);
}
