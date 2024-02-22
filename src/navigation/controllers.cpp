//========================================================================
/*!
\file    controllers.ccpp
\brief   Interface for time optimal controller implementation
\author  Daniel Meza, Jared Rosenbaum, Steven Swanbeck, (C) 2024
*/
//========================================================================

#include "controllers.h"

namespace controllers {

// -------------------------------------------------------------------------
// * 1D TIME-OPTIMAL CONTROL
// -------------------------------------------------------------------------

namespace time_optimal_1D {

Controller::Controller(vehicles::Car *car, float control_interval, float margin, float max_clearance, float curvature_sampling_interval) 
: car_(car), control_interval_(control_interval), margin_(margin), max_clearance_(max_clearance), curvature_sampling_interval_(curvature_sampling_interval) 
{}

float Controller::calculateControlSpeed(float current_speed, const float free_path_length)
{
  float control_speed {0.0};
  
//  current_speed = int(current_speed*5)/5.f;
  if ((current_speed >= car_->limits_.max_speed_ - 0.05) && (current_speed <= car_->limits_.max_speed_ + 0.05)){
	current_speed = car_->limits_.max_speed_;
  }

  // Case 1: Accelerate
  if ((current_speed < car_->limits_.max_speed_) && 
      (free_path_length >= current_speed * control_interval_ + (car_->limits_.max_acceleration_ * control_interval_) * control_interval_ / 2 + pow((current_speed + car_->limits_.max_acceleration_ * control_interval_), 2) / (2 * car_->limits_.max_acceleration_))) {
        control_speed = current_speed + car_->limits_.max_acceleration_ * control_interval_;  // speed increases by acceleration rate
  }
  // Case 2: Cruise
  // Note: For now, we are taking a small volume around car_->limits_.max_speed_. Might need to move this to navigation.cc
  else if ((current_speed == car_->limits_.max_speed_) && (free_path_length >= current_speed * control_interval_ + car_->limits_.max_speed_ * car_->limits_.max_speed_ / (2 * car_->limits_.max_acceleration_))) {
        control_speed = current_speed;
  }
  // Case 3: Decelerate
  else if (free_path_length < pow((current_speed), 2) / (2 * car_->limits_.max_acceleration_)) {
        control_speed = current_speed - car_->limits_.max_acceleration_ * control_interval_;  // speed decreases by acceleration rate
  }
  // Case 4: Declerate with expected collision warning
  else {
        control_speed = current_speed - car_->limits_.max_acceleration_ * control_interval_;  // speed decreases by acceleration rate
        std::cout << "Not enough room to decelerate! Expecting collision..." << std::endl;
        std::cout << "The free path length is: " << free_path_length << std::endl;
  }

if (control_speed < 0) { // prevent reversal
  control_speed = 0;
}
if (control_speed > car_->limits_.max_speed_) {
  control_speed = car_->limits_.max_speed_;
}
    return control_speed;
}

float Controller::calculateFreePathLength(const std::vector<Vector2f>& point_cloud, float curvature)
{
  float free_path_length {10.0f - (margin_ + (car_->dimensions_.length_ + car_->dimensions_.wheelbase_) / 2)}; // TODO set this properly
  float candidate_free_path_length {0.f};
  Vector2f point(0.0, 0.0);

  if (std::abs(curvature) < 0.01) { // Straight line case
    // Loop through point cloud
    for (int i = 0; i < (int)point_cloud.size(); i++) {
      point = point_cloud[i];

      // Update minimum free path length for lasers in front of car
      // only consider points in front of the car
      if (std::abs(point.y()) < (car_->dimensions_.width_ / 2 + margin_) && point.x() > 0) {

        // calculate candidate free path length
        float candidate_free_path_length {point.x() - (margin_ + (car_->dimensions_.length_ + car_->dimensions_.wheelbase_) / 2)};

        // see if the candidate is shorter than the actual, update if so
        if (candidate_free_path_length < free_path_length) {
          free_path_length = candidate_free_path_length;
        }
      }
    }
  } else { // Moving along an arc
    float radius {1.0f / curvature};

    // Handle right turns by symmetry
    if (curvature < 0) {
      radius *= -1;
    }

    // calculating values that will be useful so we don't have to calculate them each iteration
    float inside_rear_axle_radius {radius - (margin_ + car_->dimensions_.width_ / 2)};
    float inside_front_corner_radius {(float)sqrt(pow(radius - (margin_ + car_->dimensions_.width_ / 2), 2) + pow(margin_ + (car_->dimensions_.length_ + car_->dimensions_.wheelbase_) / 2, 2))};
    float outside_front_corner_radius {(float)sqrt(pow(radius + (margin_ + car_->dimensions_.width_ / 2), 2) + pow(margin_ + (car_->dimensions_.length_ + car_->dimensions_.wheelbase_) / 2, 2))};
    float outside_rear_corner_radius {(float)sqrt(pow(radius + (margin_ + car_->dimensions_.width_ / 2), 2) + pow(margin_ + (car_->dimensions_.length_ - car_->dimensions_.wheelbase_) / 2, 2))};
    float outside_rear_axle_radius {radius + (margin_ + car_->dimensions_.width_ / 2)};

    // Loop through point cloud
    for (int i = 0; i < (int)point_cloud.size(); i++) {
      point = point_cloud[i];
      // Handle right turns by symmetry
      if (curvature < 0) {
          point.y() *= -1;
      }

      // Check which one of the toruses the point lies within, if any
      float point_radius = sqrt(pow(point.x(), 2) + pow((radius - point.y()), 2));
      float theta = atan2(point.x(), (radius - point.y()));

      // if point radius is < minimum radius of any point on car, it will never be an obstacle
      if (point_radius < inside_rear_axle_radius) {continue;}

      // likewise, if point radius is > than the maximum radius of any point on the car, it will never be an obstacle
      if (point_radius > std::max(outside_front_corner_radius, outside_rear_corner_radius)) {continue;}

      // Condition one: The point hits the inner side of the car
      // if radius is also less than the radius of the front inside corner
      if ((point_radius >= inside_rear_axle_radius) && (point_radius < inside_front_corner_radius) && ((theta > 0))) {
        float psi = acos(inside_rear_axle_radius / point_radius);
        float phi = theta - psi;
        // std::cout << "      A" << std::endl;
        if (radius * phi < free_path_length) {
          free_path_length = radius * phi;
        }
      }

      // Condition two: The point hits the front of the car
      // if radius also falls within the radii of the front corners
      else if ((inside_front_corner_radius <= point_radius) && (point_radius < outside_front_corner_radius) && (theta > 0)) {
        float psi = asin((margin_ + (car_->dimensions_.length_ + car_->dimensions_.wheelbase_) / 2) / point_radius);
        float phi = theta - psi;
        // std::cout << "      B" << std::endl;
        if (radius * phi < free_path_length) {
          free_path_length = radius * phi;
        }
      }

      // Condition three: The point hits the outer rear side of the car
      // if radius is greater than outside rear axle radius and less than the radius of the outside rear corner
      if ((outside_rear_axle_radius <= point_radius) && (point_radius < outside_rear_corner_radius)) {
        if ((std::abs(point.x()) < margin_ + (car_->dimensions_.length_ - car_->dimensions_.wheelbase_) / 2) && (margin_ + (car_->dimensions_.width_ / 2) < std::abs(point.y()))) {
          float psi = -1 * acos(outside_rear_axle_radius / point_radius);
          float phi = theta - psi;
          if (radius * phi < free_path_length) {
            candidate_free_path_length = radius * phi;
            if (candidate_free_path_length) {}
          }
        }
      }

    }
    //TODO Limit free path length to closest point of approach
//    Vector2f goal(10.0, 0);
//    float theta = atan(goal.x()/radius);
//    if (radius*theta < free_path_length){
//      free_path_length = radius*theta;
//    }
  }
  return free_path_length;
}

float Controller::calculateClearance(const std::vector<Vector2f>& point_cloud, const float curvature, const float free_path_length)
{
  Vector2f point(0.0, 0.0);
  float min_clearance = max_clearance_; // Note: We begin with a maximum clearance range of 0.5m - any obstacles further than this will not be checked. This should be tuned. 

  if (std::abs(curvature) < 0.01) { // Straight line case
    // Loop through point cloud
    for (int i = 0; i < (int)point_cloud.size(); i++) {
      point = point_cloud[i];
      // If the point lies between the car and the obstacle at the end of the free path, and within the side of the car and the maximum clearance, check clearance. If lower, replace.
      // TODO The second part of this could use a shortening (as described in class on 2/5/24)
      if ((car_->dimensions_.width_ / 2 + margin_ <= std::abs(point.y()) && std::abs(point.y()) <= max_clearance_) && (0 <= point.x() && (point.x() <= free_path_length + car_->dimensions_.wheelbase_))) {
        float clearance = std::abs(point.y()) - car_->dimensions_.wheelbase_ / 2 - margin_;
        if (clearance < min_clearance) {
          min_clearance = clearance;
        }
      }
    }
  } else { // Moving along an arc
    
    float radius {1.0f / curvature};
    // Handle right turn
    if (curvature < 0) {
      radius *= -1;
    }

    // Loop through point cloud
    for (int i = 0; i < (int)point_cloud.size(); i++) {
      point = point_cloud[i];
      if (curvature < 0) {
          point.y() *= -1;
      }

      float point_radius = sqrt(pow(point.x(), 2) + pow((radius - point.y()), 2));
      float theta = atan2(point.x(), (radius - point.y()));
      float phi = free_path_length / radius;
      // First check the points that lie along the free path
      if ((0 <= theta && theta <= phi) && (radius - car_->dimensions_.width_ / 2 - margin_ - max_clearance_ <= point_radius && point_radius <= radius + car_->dimensions_.width_ / 2 + margin_ + max_clearance_)) {
        float clearance = std::abs(point_radius * cos(theta) - radius) - car_->dimensions_.width_ / 2 - margin_;
        if (clearance < min_clearance) {
          min_clearance = clearance;
        }
      }
      // Then, check the points that will be next to the car at its final position
      Vector2f pos = utils::transforms::transformICOM(point.x(), point.y(), phi, radius);
      if ((car_->dimensions_.width_ / 2 + margin_ <= std::abs(pos.y()) && std::abs(pos.y()) <= max_clearance_) && (0 <= pos.x() && (pos.x() <=  car_->dimensions_.wheelbase_) / 2)) {
        float clearance = std::abs(pos.y()) - car_->dimensions_.width_ / 2 - margin_;
        if (clearance < min_clearance){
          min_clearance = clearance;
        }
      }
    }
  }
  return min_clearance;
}

float Controller::calculateDistanceToGoal(const float curvature)
{
  Vector2f goal(10.0, 0.0);
  Vector2f projected_pos(0.0, 0.0);
  float goal_distance = 0;

  if (std::abs(curvature) < 0.01) {    // Straight line case
    projected_pos.x() = car_->limits_.max_speed_ * control_interval_;
    goal_distance = (goal-projected_pos).norm();
  }
  else {  // Moving along an arc
    float radius {1.0f / curvature};
    float phi = (car_->limits_.max_speed_ * control_interval_) / radius;
    projected_pos.x() = radius * sin(phi);
    projected_pos.y() = radius - (radius * cos(phi));
    goal_distance = (goal-projected_pos).norm();
  }
  return goal_distance;
}

PathCandidate Controller::evaluatePaths(const std::vector<Vector2f>& point_cloud)
{
  // creating starting path (with terrible score)
  auto best_path {PathCandidate(-100)};

  // weights
  float w1{8.f}, w2{-0.5f};

  // Evaluate all possible paths and select optimal option
  for (float path_curvature = -1 * (car_->limits_.max_curvature_); path_curvature <= car_->limits_.max_curvature_; path_curvature += curvature_sampling_interval_) {
    
    // create candidate for this path
    PathCandidate candidate;
    candidate.curvature = path_curvature;

    // calculate free path length
    candidate.free_path_length = Controller::calculateFreePathLength(point_cloud, candidate.curvature);
    
    // calculate clearance
    candidate.clearance = Controller::calculateClearance(point_cloud, candidate.curvature, candidate.free_path_length);

    // goal distance metric
    candidate.goal_distance = Controller::calculateDistanceToGoal(candidate.curvature);

    // Calculate score and update selection
    candidate.score = candidate.free_path_length + w1 * candidate.clearance + w2 * candidate.goal_distance;
    
    if (candidate.score > best_path.score) {
      best_path = candidate;
    }
  }
  // return best path
  return best_path;
}

Command Controller::generateCommand(const std::vector<Vector2f>& point_cloud, const float current_speed)
{
  PathCandidate path {Controller::evaluatePaths(point_cloud)};
  float speed {Controller::calculateControlSpeed(current_speed, path.free_path_length)};
  // std::cout << "FPL: " << path.free_path_length << ", " << "Current speed: " << current_speed << ", " << "Commanded speed: " << speed << std::endl;
  return Command(speed, path.curvature);
}

float Controller::getControlInterval()
{
  return control_interval_;
}

} // namespace time_optimal_1D

// -------------------------------------------------------------------------
// * LATENCY COMPENSATION
// -------------------------------------------------------------------------

namespace latency_compensation {

// -------------------------------------------------------------------------
// & constructor & destructor
// -------------------------------------------------------------------------
// Controller::Controller(vehicles::Car car, float control_interval, float margin, float max_clearance, float curvature_sampling_interval, float latency) : car_(car), latency_(latency)
Controller::Controller(vehicles::Car *car, float control_interval, float margin, float max_clearance, float curvature_sampling_interval, float latency) : latency_(latency)
{
  // create a new TimeOptimalController to use
  toc_ = new time_optimal_1D::Controller(car, control_interval, margin, max_clearance, curvature_sampling_interval);
}

Controller::~Controller()
{
  delete toc_;
}

// -------------------------------------------------------------------------
// & adding to command history
// -------------------------------------------------------------------------
void Controller::recordCommand(const CommandStamped command)
{
  // add the command to the command history
  command_history_.push_back(command);
  // std::cout << "New command recorded for timestamp " << command.timestamp << std::endl;
}

void Controller::recordCommand(const time_optimal_1D::Command command)
{
  // add the command to the command history
  Controller::recordCommand(CommandStamped(command));
}

// -------------------------------------------------------------------------
// & projecting forward
// -------------------------------------------------------------------------

time_optimal_1D::Command Controller::generateCommand(const std::vector<Vector2f>& point_cloud, const float current_speed, const double last_data_timestamp)
{
  // using latency, and history of results, project the car's position and velocity forward through time; search the controls queue and pop until a timestamp is newer than the current time
  State2D projected_state {Controller::projectState(current_speed, last_data_timestamp)};

  // use this forward projection to transform the point cloud
  auto cloud {Controller::transformCloud(point_cloud, projected_state)};

  // feed these updated parameters into the 1D time-optimal controller
  time_optimal_1D::Command command {toc_->generateCommand(cloud, projected_state.speed)};

  // receive a response from the 1D TOC and record it, then bubble it back out to the main
  Controller::recordCommand(command);

  return command;
}

State2D Controller::projectState(const float current_speed, const double last_msg_timestamp)
{
  // setting state to reflect the starting state of the robot
  State2D state;
  state.speed = current_speed;
  state.position = Eigen::Vector2f {0.f, 0.f};
  state.theta = 0;

  if (command_history_.size() < 1) {
    return state;
  }

  double time_threshold {ros::Time::now().toSec()};
  while (!command_history_.empty()) {
    if ((time_threshold - command_history_.front().timestamp) < latency_) {
      break;
    }
//    std::cout << "Removing command with diff " << time_threshold - command_history_.front().timestamp << " from command history for latency " << latency_ << std::endl;
    command_history_.pop_front();
  }

  // project the future state of the car
//  std::cout << "Considering " << command_history_.size() << " previous commands to compensate for latency..." << std::endl;
  for (const auto &command : command_history_) {
    // std::cout << "Latency: " << latency_ << ", Diff: " << command.timestamp - last_msg_timestamp << std::endl;
    double distance_traveled {command.command.velocity * toc_->getControlInterval()};

    // std::cout << "Speed: " << command.command.velocity << ", Curvature: " << command.command.curvature << std::endl;
    if (std::abs(command.command.curvature) > 0.01) { // updating for curved case
      double radius {1 / command.command.curvature};
      double theta {distance_traveled / radius};
      state.position.x() += distance_traveled * cos(theta);
      state.position.y() += distance_traveled * sin(theta);
      state.theta += theta;
    } else { // updating for straight case
      state.position.x() += distance_traveled;
    }
    state.speed = command.command.velocity;

    // std::cout << "Updated state: ";
    // Controller::printState(state);
  }

  // std::cout << "Returning state: ";
  // Controller::printState(state);
  return state;
}

void Controller::printState(const State2D &state)
{
  std::cout << "State is: \n\tPosition:\t(" << state.position.x() << ", " << state.position.y() << ")\n\tTheta:\t\t" << state.theta << "\n\tSpeed:\t\t" << state.speed << std::endl;
}

std::vector<Eigen::Vector2f> Controller::transformCloud(std::vector<Eigen::Vector2f> cloud, const State2D &state)
{
  // generate a transformation matrix from projected state
  Eigen::Matrix3f transformation_matrix;
  transformation_matrix << 
        cos(state.theta), -1 * sin(state.theta), state.position.x(),
        sin(state.theta),      cos(state.theta), state.position.y(),
                       0,                     0,                  1;

  // TODO expressly taking the inverse is more expensive than it needs to be, consider replacing this with a more efficient split-up calculation
  auto inv_transformation_matrix {transformation_matrix.inverse()};

  // use it to transform the cloud points
  for (std::size_t i = 0; i < cloud.size(); i++) {
    // std::cout << "Point before: " << cloud[i].transpose() << std::endl;
    Eigen::Vector3f augmented_point {cloud[i].x(), cloud[i].y(), 1};
    Eigen::Vector3f transformed_point {inv_transformation_matrix * augmented_point};
    cloud[i] = Eigen::Vector2f(transformed_point.x(), transformed_point.y());
    // std::cout << "Point after: " << cloud[i].transpose() << std::endl;
  }
  return cloud;
}

float Controller::calculateFreePathLength(const std::vector<Vector2f>& point_cloud, const float curvature, const double last_data_timestamp)
{
  // using latency, and history of results, project the car's position and velocity forward through time; search the controls queue and pop until a timestamp is newer than the current time
  State2D projected_state {Controller::projectState(0.f, last_data_timestamp)};

  // use this forward projection to transform the point cloud
  auto cloud {Controller::transformCloud(point_cloud, projected_state)};

  // feed these updated parameters into the 1D time-optimal controller
  float fpl {toc_->calculateFreePathLength(cloud, curvature)};
  return fpl;
}



} // namespace latency_compensation

} // namespace controllers

// -------------------------------------------------------------------------
// * FIN
// -------------------------------------------------------------------------
