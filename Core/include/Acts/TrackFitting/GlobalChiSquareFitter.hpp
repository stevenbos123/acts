// This file is part of the Acts project.
//
// Copyright (C) 2023 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

// Workaround for building on clang+libstdc++
#include "Acts/Utilities/detail/ReferenceWrapperAnyCompat.hpp"

#include "Acts/Definitions/Algebra.hpp"
#include "Acts/EventData/Measurement.hpp"
#include "Acts/EventData/MeasurementHelpers.hpp"
#include "Acts/EventData/MultiTrajectory.hpp"
#include "Acts/EventData/MultiTrajectoryHelpers.hpp"
#include "Acts/EventData/SourceLink.hpp"
#include "Acts/EventData/TrackHelpers.hpp"
#include "Acts/EventData/TrackParameters.hpp"
#include "Acts/EventData/VectorMultiTrajectory.hpp"
#include "Acts/Geometry/GeometryContext.hpp"
#include "Acts/MagneticField/MagneticFieldContext.hpp"
#include "Acts/Material/MaterialSlab.hpp"
#include "Acts/Propagator/AbortList.hpp"
#include "Acts/Propagator/ActionList.hpp"
#include "Acts/Propagator/ConstrainedStep.hpp"
#include "Acts/Propagator/DirectNavigator.hpp"
#include "Acts/Propagator/Navigator.hpp"
#include "Acts/Propagator/Propagator.hpp"
#include "Acts/Propagator/StandardAborters.hpp"
#include "Acts/Propagator/StraightLineStepper.hpp"
#include "Acts/Propagator/detail/PointwiseMaterialInteraction.hpp"
#include "Acts/TrackFitting/detail/KalmanUpdateHelpers.hpp"
#include "Acts/TrackFitting/detail/VoidKalmanComponents.hpp"
#include "Acts/Utilities/CalibrationContext.hpp"
#include "Acts/Utilities/Delegate.hpp"
#include "Acts/Utilities/Logger.hpp"
#include "Acts/Utilities/Result.hpp"

#include <functional>
#include <map>
#include <memory>

namespace Acts {
namespace Experimental {

/// Extension struct which holds delegates to customize the KF behavior
template <typename traj_t>
struct Gx2FitterExtensions {
  using TrackStateProxy = typename MultiTrajectory<traj_t>::TrackStateProxy;
  using ConstTrackStateProxy =
      typename MultiTrajectory<traj_t>::ConstTrackStateProxy;
  using Parameters = typename TrackStateProxy::Parameters;

  using Calibrator = Delegate<void(const GeometryContext&, const SourceLink&,
                                   TrackStateProxy)>;

  using Updater = Delegate<Result<void>(const GeometryContext&, TrackStateProxy,
                                        Direction, const Logger&)>;

  using OutlierFinder = Delegate<bool(ConstTrackStateProxy)>;

  /// The Calibrator is a dedicated calibration algorithm that allows
  /// to calibrate measurements using track information, this could be
  /// e.g. sagging for wires, module deformations, etc.
  Calibrator calibrator;

  /// The updater incorporates measurement information into the track parameters
  Updater updater;

  /// Determines whether a measurement is supposed to be considered as an
  /// outlier
  OutlierFinder outlierFinder;

  // TODO get an own Calibrator and Updater
  /// Default constructor which connects the default void components
  Gx2FitterExtensions() {
    calibrator.template connect<&voidKalmanCalibrator<traj_t>>();
    updater.template connect<&voidKalmanUpdater<traj_t>>();
    outlierFinder.template connect<&voidOutlierFinder<traj_t>>();
  }
};

/// Combined options for the Global-Chi-Square fitter.
///
/// @tparam traj_t The trajectory type
template <typename traj_t>
struct Gx2FitterOptions {
  /// PropagatorOptions with context.
  ///
  /// @param gctx The geometry context for this fit
  /// @param mctx The magnetic context for this fit
  /// @param cctx The calibration context for this fit
  /// @param extensions_ The KF extensions
  /// @param pOptions The plain propagator options
  /// @param rSurface The reference surface for the fit to be expressed at
  /// @param mScattering Whether to include multiple scattering
  /// @param eLoss Whether to include energy loss
  /// @param freeToBoundCorrection_ Correction for non-linearity effect during transform from free to bound
  /// @param nUpdateMax_ Max number of iterations for updating the parameters
  Gx2FitterOptions(const GeometryContext& gctx,
                   const MagneticFieldContext& mctx,
                   std::reference_wrapper<const CalibrationContext> cctx,
                   Gx2FitterExtensions<traj_t> extensions_,
                   const PropagatorPlainOptions& pOptions,
                   const Surface* rSurface = nullptr, bool mScattering = false,
                   bool eLoss = false,
                   const FreeToBoundCorrection& freeToBoundCorrection_ =
                       FreeToBoundCorrection(false),
                   const size_t nUpdateMax_ = 5)
      : geoContext(gctx),
        magFieldContext(mctx),
        calibrationContext(cctx),
        extensions(extensions_),
        propagatorPlainOptions(pOptions),
        referenceSurface(rSurface),
        multipleScattering(mScattering),
        energyLoss(eLoss),
        freeToBoundCorrection(freeToBoundCorrection_),
        nUpdateMax(nUpdateMax_) {}

  /// Contexts are required and the options must not be default-constructible.
  Gx2FitterOptions() = delete;

  /// Context object for the geometry
  std::reference_wrapper<const GeometryContext> geoContext;
  /// Context object for the magnetic field
  std::reference_wrapper<const MagneticFieldContext> magFieldContext;
  /// context object for the calibration
  std::reference_wrapper<const CalibrationContext> calibrationContext;

  Gx2FitterExtensions<traj_t> extensions;

  /// The trivial propagator options
  PropagatorPlainOptions propagatorPlainOptions;

  /// The reference Surface
  const Surface* referenceSurface = nullptr;

  /// Whether to consider multiple scattering
  bool multipleScattering = false;

  /// Whether to consider energy loss
  bool energyLoss = false;

  /// Whether to include non-linear correction during global to local
  /// transformation
  FreeToBoundCorrection freeToBoundCorrection;

  /// Max number of iterations during the fit
  size_t nUpdateMax = 5;
};

template <typename traj_t>
struct Gx2FitterResult {
  // Fitted states that the actor has handled.
  traj_t* fittedStates{nullptr};

  // This is the index of the 'tip' of the track stored in multitrajectory.
  // This corresponds to the last measurement state in the multitrajectory.
  // Since this KF only stores one trajectory, it is unambiguous.
  // SIZE_MAX is the start of a trajectory.
  size_t lastMeasurementIndex = Acts::MultiTrajectoryTraits::kInvalid;

  // This is the index of the 'tip' of the states stored in multitrajectory.
  // This corresponds to the last state in the multitrajectory.
  // Since this KF only stores one trajectory, it is unambiguous.
  // SIZE_MAX is the start of a trajectory.
  size_t lastTrackIndex = Acts::MultiTrajectoryTraits::kInvalid;

  // The optional Parameters at the provided surface
  std::optional<BoundTrackParameters> fittedParameters;

  // Counter for states with non-outlier measurements
  size_t measurementStates = 0;

  // Counter for measurements holes
  // A hole correspond to a surface with an associated detector element with no
  // associated measurement. Holes are only taken into account if they are
  // between the first and last measurements.
  size_t measurementHoles = 0;

  // Counter for handled states
  size_t processedStates = 0;

  // Indicator if track fitting has been done
  bool finished = false;

  // Measurement surfaces without hits
  std::vector<const Surface*> missedActiveSurfaces;

  // Measurement surfaces handled in both forward and
  // backward filtering
  std::vector<const Surface*> passedAgainSurfaces;

  Result<void> result{Result<void>::success()};

  // collectors
  std::vector<ActsVector<2>> collectorResiduals;
  std::vector<ActsSquareMatrix<2>> collectorCovariance;
  std::vector<BoundMatrix> collectorJacobians;

  BoundMatrix jacobianFromStart = BoundMatrix::Identity();

  // Count how many surfaces have been hit
  size_t surfaceCount = 0;
};

/// Global Chi Square fitter (GX2F) implementation.
///
/// @tparam propagator_t Type of the propagation class
///
/// TODO Write description
template <typename propagator_t, typename traj_t>
class Gx2Fitter {
  /// The navigator type
  using Gx2fNavigator = typename propagator_t::Navigator;

  /// The navigator has DirectNavigator type or not
  static constexpr bool isDirectNavigator =
      std::is_same<Gx2fNavigator, DirectNavigator>::value;

 public:
  Gx2Fitter(propagator_t pPropagator,
            std::unique_ptr<const Logger> _logger =
                getDefaultLogger("Gx2Fitter", Logging::INFO))
      : m_propagator(std::move(pPropagator)),
        m_logger{std::move(_logger)},
        m_actorLogger{m_logger->cloneWithSuffix("Actor")} {}

 private:
  /// The propagator for the transport and material update
  propagator_t m_propagator;

  /// The logger instance
  std::unique_ptr<const Logger> m_logger;
  std::unique_ptr<const Logger> m_actorLogger;

  const Logger& logger() const { return *m_logger; }

  /// @brief Propagator Actor plugin for the GX2F
  ///
  /// @tparam parameters_t The type of parameters used for "local" parameters.
  /// @tparam calibrator_t The type of calibrator
  /// @tparam outlier_finder_t Type of the outlier finder class
  ///
  /// The GX2FnActor does not rely on the measurements to be
  /// sorted along the track. /// TODO is this true?
  template <typename parameters_t>
  class Actor {
   public:
    /// Broadcast the result_type
    using result_type = Gx2FitterResult<traj_t>;

    /// The target surface
    const Surface* targetSurface = nullptr;

    /// Allows retrieving measurements for a surface
    const std::map<GeometryIdentifier, SourceLink>* inputMeasurements = nullptr;

    /// Whether to consider multiple scattering.
    bool multipleScattering = false;  /// TODO implement later

    /// Whether to consider energy loss.
    bool energyLoss = false;  /// TODO implement later

    /// Whether to include non-linear correction during global to local
    /// transformation
    FreeToBoundCorrection freeToBoundCorrection;

    /// Input MultiTrajectory
    std::shared_ptr<MultiTrajectory<traj_t>> outputStates;

    /// The logger instance
    const Logger* actorLogger{nullptr};

    /// Logger helper
    const Logger& logger() const { return *actorLogger; }

    Gx2FitterExtensions<traj_t> extensions;

    /// The Surface being
    SurfaceReached targetReached;

    /// @brief Gx2f actor operation
    ///
    /// @tparam propagator_state_t is the type of Propagator state
    /// @tparam stepper_t Type of the stepper
    /// @tparam navigator_t Type of the navigator
    ///
    /// @param state is the mutable propagator state object
    /// @param stepper The stepper in use
    /// @param navigator The navigator in use
    /// @param result is the mutable result state object
    template <typename propagator_state_t, typename stepper_t,
              typename navigator_t>
    void operator()(propagator_state_t& state, const stepper_t& stepper,
                    const navigator_t& navigator, result_type& result,
                    const Logger& /*logger*/) const {
      assert(result.fittedStates && "No MultiTrajectory set");

      if (result.finished) {
        return;
      }

      // Add the measurement surface as external surface to navigator.
      // We will try to hit those surface by ignoring boundary checks.
      if constexpr (not isDirectNavigator) {
        if (result.processedStates == 0) {
          for (auto measurementIt = inputMeasurements->begin();
               measurementIt != inputMeasurements->end(); measurementIt++) {
            navigator.insertExternalSurface(state.navigation,
                                            measurementIt->first);
          }
        }
      }

      // Update:
      // - Waiting for a current surface
      auto surface = navigator.currentSurface(state.navigation);
      //      std::string direction = state.stepping.navDir.toString();
      if (surface != nullptr) {
        ++result.surfaceCount;
        ACTS_VERBOSE("Measurement surface " << surface->geometryId()
                                            << " detected.");

        // check if measurement surface
        auto sourcelink_it = inputMeasurements->find(surface->geometryId());

        if (sourcelink_it != inputMeasurements->end()) {
          stepper.transportCovarianceToBound(state.stepping, *surface,
                                             freeToBoundCorrection);
          auto res = stepper.boundState(state.stepping, *surface, false,
                                        freeToBoundCorrection);
          if (!res.ok()) {
            std::cout << "dbgActor: res = stepper.boundState res not ok"
                      << std::endl;
            return;
          }
          auto& [boundParams, jacobian, pathLength] = *res;
          result.jacobianFromStart = jacobian * result.jacobianFromStart;

          // add a full TrackState entry multi trajectory
          // (this allocates storage for all components, we will set them later)
          auto fittedStates = *result.fittedStates;
          const auto newTrackIndex = fittedStates.addTrackState(
              TrackStatePropMask::All, result.lastTrackIndex);

          // now get track state proxy back
          auto trackStateProxy = fittedStates.getTrackState(newTrackIndex);
          trackStateProxy.setReferenceSurface(surface->getSharedPtr());
          // assign the source link to the track state
          trackStateProxy.setUncalibratedSourceLink(sourcelink_it->second);

          // Fill the track state
          trackStateProxy.predicted() = std::move(boundParams.parameters());
          auto predicted = trackStateProxy.predicted();

          // We have predicted parameters, so calibrate the uncalibrated input
          // measurement
          extensions.calibrator(state.geoContext, sourcelink_it->second,
                                trackStateProxy);

          const size_t measdimPlaceholder = 2;
          auto measurement =
              trackStateProxy.template calibrated<measdimPlaceholder>();
          auto covarianceMeasurement =
              trackStateProxy
                  .template calibratedCovariance<measdimPlaceholder>();
          // calculate residuals and return with covariances and jacobians
          ActsVector<2> residual;
          for (long i = 0; i < measurement.size(); i++) {
            residual[i] = measurement[i] - predicted[i];
          }
          ACTS_VERBOSE("Measurement in Actor:\n" << measurement);
          result.collectorResiduals.push_back(residual);
          result.collectorCovariance.push_back(covarianceMeasurement);

          if (boundParams.covariance().has_value()) {
            trackStateProxy.predictedCovariance() =
                std::move(*boundParams.covariance());
          }

          result.collectorJacobians.push_back(result.jacobianFromStart);

          trackStateProxy.jacobian() = std::move(jacobian);
          trackStateProxy.pathLength() = std::move(pathLength);
        }
      }

      if (result.surfaceCount > 11) {
        ACTS_INFO("Actor: finish due to limit. Result might be garbage.");
        result.finished = true;
      }
    }
  };

  /// Aborter can stay like this probably
  template <typename parameters_t>
  class Aborter {
   public:
    /// Broadcast the result_type
    using action_type = Actor<parameters_t>;

    template <typename propagator_state_t, typename stepper_t,
              typename navigator_t, typename result_t>
    bool operator()(propagator_state_t& /*state*/, const stepper_t& /*stepper*/,
                    const navigator_t& /*navigator*/, const result_t& result,
                    const Logger& /*logger*/) const {
      if (!result.result.ok() or result.finished) {
        return true;
      }
      return false;
    }
  };

 public:
  /// Fit implementation
  ///
  /// @tparam source_link_iterator_t Iterator type used to pass source links
  /// @tparam start_parameters_t Type of the initial parameters
  /// @tparam parameters_t Type of parameters used for local parameters
  /// @tparam track_container_t Type of the track container backend
  /// @tparam holder_t Type defining track container backend ownership
  ///
  /// @param it Begin iterator for the fittable uncalibrated measurements
  /// @param end End iterator for the fittable uncalibrated measurements
  /// @param sParameters The initial track parameters
  /// @param gx2fOptions Gx2FitterOptions steering the fit
  /// @param trackContainer Input track container storage to append into
  /// @note The input measurements are given in the form of @c SourceLink s.
  /// It's the calibrators job to turn them into calibrated measurements used in
  /// the fit.
  ///
  /// @return the output as an output track
  template <typename source_link_iterator_t, typename start_parameters_t,
            typename parameters_t = BoundTrackParameters,
            typename track_container_t, template <typename> class holder_t,
            bool _isdn = isDirectNavigator>
  auto fit(source_link_iterator_t it, source_link_iterator_t end,
           const start_parameters_t& sParameters,
           const Gx2FitterOptions<traj_t>& gx2fOptions,
           TrackContainer<track_container_t, traj_t, holder_t>& trackContainer)
      const -> std::enable_if_t<
          !_isdn, Result<typename TrackContainer<track_container_t, traj_t,
                                                 holder_t>::TrackProxy>> {
    // Preprocess Measurements (Sourcelinks -> map)
    // To be able to find measurements later, we put them into a map
    // We need to copy input SourceLinks anyway, so the map can own them.
    ACTS_VERBOSE("Preparing " << std::distance(it, end)
                              << " input measurements");
    std::map<GeometryIdentifier, SourceLink> inputMeasurements;

    for (; it != end; ++it) {
      SourceLink sl = *it;
      auto geoId = sl.geometryId();
      inputMeasurements.emplace(geoId, std::move(sl));
    }
    ACTS_VERBOSE("inputMeasurements.size() = " << inputMeasurements.size());

    /// Fully understand Aborter, Actor, Result later
    // Create the ActionList and AbortList
    using GX2FAborter = Aborter<parameters_t>;
    using GX2FActor = Actor<parameters_t>;

    using GX2FResult = typename GX2FActor::result_type;
    using Actors = Acts::ActionList<GX2FActor>;
    using Aborters = Acts::AbortList<GX2FAborter>;

    using PropagatorOptions = Acts::PropagatorOptions<Actors, Aborters>;

    const size_t reducedMatrixSize = 4;
    Acts::CurvilinearTrackParameters params = sParameters;
    BoundVector deltaParams = BoundVector::Zero();
    double chi2sum = 0;
    BoundMatrix aMatrix = BoundMatrix::Zero();
    BoundVector bVector = BoundVector::Zero();

    ACTS_VERBOSE("params:\n" << params);

    /// Actual Fitting /////////////////////////////////////////////////////////
    ACTS_DEBUG("Start to iterate");

    // Iterate the fit and improve result. Abort after n steps or after
    // convergence
    for (size_t nUpdate = 0; nUpdate < gx2fOptions.nUpdateMax; nUpdate++) {
      ACTS_VERBOSE("nUpdate = " << nUpdate + 1 << "/"
                                << gx2fOptions.nUpdateMax);

      // update params
      params.parameters() += deltaParams;
      ACTS_VERBOSE("updated params:\n" << params);

      // set up propagator and co
      Acts::GeometryContext geoCtx = gx2fOptions.geoContext;
      Acts::MagneticFieldContext magCtx = gx2fOptions.magFieldContext;
      // Set options for propagator
      PropagatorOptions propagatorOptions(geoCtx, magCtx);
      auto& gx2fActor = propagatorOptions.actionList.template get<GX2FActor>();
      gx2fActor.inputMeasurements = &inputMeasurements;
      gx2fActor.extensions = gx2fOptions.extensions;
      gx2fActor.actorLogger = m_actorLogger.get();

      typename propagator_t::template action_list_t_result_t<
          CurvilinearTrackParameters, Actors>
          inputResult;

      auto& r = inputResult.template get<Gx2FitterResult<traj_t>>();

      r.fittedStates = &trackContainer.trackStateContainer();
      // propagate with params and return jacobians and residuals
      auto result = m_propagator.template propagate(params, propagatorOptions,
                                                    std::move(inputResult));

      // TODO Improve Propagator + Actor [allocate before loop], rewrite
      // makeMeasurements
      auto& propRes = *result;
      auto gx2fResult = std::move(propRes.template get<GX2FResult>());

      ACTS_VERBOSE("gx2fResult.collectorResiduals.size() = "
                   << gx2fResult.collectorResiduals.size());
      ACTS_VERBOSE("gx2fResult.collectorCovariance.size() = "
                   << gx2fResult.collectorCovariance.size());
      ACTS_VERBOSE("gx2fResult.collectorJacobians.size() = "
                   << gx2fResult.collectorJacobians.size());

      chi2sum = 0;
      aMatrix = BoundMatrix::Zero();
      bVector = BoundVector::Zero();

      // TODO generalize for non-2D measurements
      for (size_t iMeas = 0; iMeas < gx2fResult.collectorResiduals.size();
           iMeas++) {
        ActsMatrix<2, eBoundSize> proj;

        for (size_t i_ = 0; i_ < 2; i_++) {
          for (size_t j_ = 0; j_ < eBoundSize; j_++) {
            proj(i_, j_) = 0;
          }
        }
        proj(0, 0) = 1;
        proj(1, 1) = 1;

        const auto ri = gx2fResult.collectorResiduals[iMeas];
        const auto covi = gx2fResult.collectorCovariance[iMeas];
        const auto coviInv = covi.inverse();
        const auto projectedJacobian =
            proj * gx2fResult.collectorJacobians[iMeas];

        const double chi2meas = (ri.transpose() * coviInv * ri).eval()(0);
        const BoundMatrix aMatrixMeas =
            projectedJacobian.transpose() * coviInv * projectedJacobian;
        const BoundVector bVectorMeas =
            projectedJacobian.transpose() * coviInv * ri;

        chi2sum += chi2meas;
        aMatrix += aMatrixMeas;
        bVector += bVectorMeas;
      }

      // calculate delta params [a] * delta = b
      deltaParams = BoundVector::Zero();
      const ActsVector<reducedMatrixSize> deltaParamsReduced =
          aMatrix.topLeftCorner<reducedMatrixSize, reducedMatrixSize>()
              .colPivHouseholderQr()
              .solve(bVector.topLeftCorner<reducedMatrixSize, 1>());

      for (size_t idp = 0; idp < reducedMatrixSize; idp++) {
        deltaParams(idp, 0) = deltaParamsReduced(idp, 0);
      }

      ACTS_VERBOSE("chi2sum = " << chi2sum);
      ACTS_VERBOSE("aMatrix:\n" << aMatrix);
      ACTS_VERBOSE("bVector:\n" << bVector);
      ACTS_VERBOSE("deltaParams:\n" << deltaParams);

      // TODO check delta params and abort
      // similar to:
      // if (sum(delta_params) < 1e-3) {
      //   break;
      // }
    }
    ACTS_DEBUG("Finished to iterate");
    /// Finish Fitting /////////////////////////////////////////////////////////

    // Calculate covariance of the fitted parameters with inverse of [a]
    BoundMatrix fullCovariancePredicted = BoundMatrix::Identity();
    if (aMatrix.topLeftCorner<reducedMatrixSize, reducedMatrixSize>()
            .determinant() != 0) {
      fullCovariancePredicted
          .template topLeftCorner<reducedMatrixSize, reducedMatrixSize>() =
          aMatrix.topLeftCorner<reducedMatrixSize, reducedMatrixSize>()
              .inverse();
    } else if (gx2fOptions.nUpdateMax > 0) {
      // TODO
      std::cout << "det(a) == 0. This shouldn't happen. Implement real ERROR"
                << std::endl;
    }

    // Prepare track for return
    auto track = trackContainer.getTrack(trackContainer.addTrack());
    track.parameters() = params.parameters();
    track.covariance() = fullCovariancePredicted;
    // TODO track.tipIndex() = gx2fResult.lastMeasurementIndex;
    // TODO track.setReferenceSurface(params.referenceSurface().getSharedPtr());
    // TODO track.nMeasurements() = gx2fResult.measurementStates;
    // TODO track.nHoles() = gx2fResult.measurementHoles;
    calculateTrackQuantities(
        track);  // TODO write test for calculateTrackQuantities

    // Return the converted Track
    return track;
  }
};
}  // namespace Experimental
}  // namespace Acts
