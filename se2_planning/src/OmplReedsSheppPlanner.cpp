/*
 * OmplReedsSheppPlanner.cpp
 *
 *  Created on: Apr 1, 2020
 *      Author: jelavice
 */

#include "se2_planning/OmplReedsSheppPlanner.hpp"

#include <memory>

#include "ompl/base/spaces/ReedsSheppStateSpace.h"

#include "ompl/base/Planner.h"
#include "ompl/base/objectives/PathLengthOptimizationObjective.h"
#include "ompl/geometric/planners/rrt/RRTstar.h"

namespace se2_planning {

template <typename T>
int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

bool OmplReedsSheppPlanner::initialize() {
  // todo load from somewhere
  const double turningRadius = 1.0;
  stateSpace_.reset(new ompl::base::ReedsSheppStateSpace(turningRadius));
  simpleSetup_.reset(new ompl::geometric::SimpleSetup(stateSpace_));
  const int statSpaceDim = 2;
  bounds_ = std::make_unique<ompl::base::RealVectorBounds>(statSpaceDim);
  setStateSpaceBoundaries();
  auto si = simpleSetup_->getSpaceInformation();
  auto planner = std::make_shared<ompl::geometric::RRTstar>(si);
  const double range = 15.0;
  // todo read this from somewhere
  planner->setRange(range);
  simpleSetup_->setPlanner(planner);
  ompl::base::OptimizationObjectivePtr optimizationObjective(std::make_shared<ompl::base::PathLengthOptimizationObjective>(si));
  simpleSetup_->setOptimizationObjective(optimizationObjective);

  BASE::initialize();

  std::cout << "Initialized" << std::endl;
  return true;
}
bool OmplReedsSheppPlanner::plan() {
  std::cout << "Planning in OmplReedsSheppPlanner" << std::endl;
  return BASE::plan();
}
void OmplReedsSheppPlanner::setStateSpaceBoundaries() {
  // todo load from somewhere
  const double bx = 1000.0;
  const double by = 1000.0;
  bounds_->low[0] = -by / 2 - 0.1;
  bounds_->low[1] = -bx / 2 - 0.1;
  bounds_->high[0] = by / 2 + 0.1;
  bounds_->high[1] = bx / 2 + 0.1;
  stateSpace_->as<ompl::base::SE2StateSpace>()->setBounds(*bounds_);
}
bool OmplReedsSheppPlanner::isStateValid(const ompl::base::SpaceInformation* si, const ompl::base::State* state) {
  return true;
}
ompl::base::ScopedStatePtr OmplReedsSheppPlanner::convert(const State& state) const {
  ompl::base::ScopedStatePtr stateOmpl(std::make_shared<ompl::base::ScopedState<> >(stateSpace_));
  auto s = ((*stateOmpl)())->as<ompl::base::SE2StateSpace::StateType>();
  auto rsState = state.as<ReedsSheppState>();
  s->setX(rsState->x_);
  s->setY(rsState->y_);
  s->setYaw(rsState->yaw_);

  return stateOmpl;
}

void OmplReedsSheppPlanner::convert(const ompl::geometric::PathGeometric& pathOmpl, Path* path) const {
  using Direction = ReedsSheppPathSegment::Direction;
  auto interpolatedPath = pathOmpl;
  // todo get from params
  const double resolution = 0.05;
  const unsigned int numPoints = static_cast<unsigned int>(std::ceil(std::fabs(pathOmpl.length()) / resolution));
  interpolatedPath.interpolate(numPoints);
  std::cout << "Here 11" << std::endl;
  std::cout << "Num points: " << numPoints << std::endl;
  std::cout << "Length: " << pathOmpl.length() << std::endl;
  std::cout << "num points in the input path: " << pathOmpl.getStateCount() << std::endl;
  unsigned int idStart = 0;
  ReedsSheppPathSegment::Direction prevDirection;
  for (; idStart < interpolatedPath.getStateCount(); ++idStart) {
    const int sign = getDistanceSignAt(interpolatedPath, idStart);
    if (sign != 0) {
      switch (sign) {
        case 1: {
          prevDirection = Direction::FWD;
          break;
        }
        case -1: {
          prevDirection = Direction::BCK;
          break;
        }
      }
      break;  // break for
    }
  }
  std::cout << "Here 12" << std::endl;
  auto returnPath = path->as<ReedsSheppPath>();
  returnPath->segment_.clear();
  returnPath->segment_.reserve(interpolatedPath.getStateCount());
  std::cout << "Here 121" << std::endl;
  std::cout << "IdStart " << idStart << std::endl;
  auto stateOmpl = interpolatedPath.getState(idStart);
  ReedsSheppState point = se2_planning::convert(stateOmpl);
  std::cout << "Here 122" << std::endl;

  ReedsSheppPathSegment currentSegment;
  currentSegment.point_.push_back(point);
  currentSegment.direction_ = prevDirection;
  auto currDirection = prevDirection;
  const int lastElemId = interpolatedPath.getStateCount() - 1;
  std::cout << "Here 13" << std::endl;
  for (unsigned int i = idStart + 1; i < lastElemId; i++) {
    const int sign = getDistanceSignAt(interpolatedPath, idStart);
    switch (sign) {
      case 0: {
        continue;
      }
      case 1: {
        currDirection = Direction::FWD;
        break;
      }
      case -1: {
        currDirection = Direction::BCK;
        break;
      }
    }
    ReedsSheppState point = se2_planning::convert(interpolatedPath.getState(i));
    if (currDirection != prevDirection) {
      returnPath->segment_.push_back(currentSegment);
      currentSegment.direction_ = currDirection;
      currentSegment.point_.clear();
      currentSegment.point_.reserve(numPoints);
      currentSegment.point_.push_back(point);
    } else {
      currentSegment.point_.push_back(point);
    }

    if (i == lastElemId - 1) {
      currentSegment.point_.push_back(se2_planning::convert(interpolatedPath.getState(i + 1)));
      returnPath->segment_.push_back(currentSegment);
      break;
    }

    prevDirection = currDirection;
  }
  std::cout << "Here 15" << std::endl;
}

int OmplReedsSheppPlanner::getDistanceSignAt(const ompl::geometric::PathGeometric& path, unsigned int id) const {
  const ompl::base::State* currState = path.getState(id);
  const ompl::base::State* stateNext = path.getState(id + 1);
  const auto rsPath = stateSpace_->as<ompl::base::ReedsSheppStateSpace>()->reedsShepp(currState, stateNext);
  const double longestSegment = getLongestSegment(rsPath.length_, 5);
  return sgn(longestSegment);
}

double getLongestSegment(const double* array, int N) {
  double pathLengths[5];
  std::transform(array, array + N, pathLengths, [](double l) -> double { return std::fabs(l); });
  double* maxElemIt = std::max_element(pathLengths, pathLengths + N);
  const int maxElemId = maxElemIt - pathLengths;
  return array[maxElemId];
}

ReedsSheppState convert(const ompl::base::State* s) {
  if (s == nullptr) {
    std::cout << "s is null ptr" << std::endl;
  }
  auto rsState = s->as<ompl::base::SE2StateSpace::StateType>();
  ReedsSheppState retState;
  if (rsState == nullptr) {
    std::cout << "cast failed state is null ptr" << std::endl;
  }
  retState.x_ = rsState->getX();
  retState.y_ = rsState->getY();
  retState.yaw_ = rsState->getYaw();

  return retState;
}

} /* namespace se2_planning */
