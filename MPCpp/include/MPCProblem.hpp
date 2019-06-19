#pragma once

#include "optimizationProblem.hpp"
#include "activeModelMPC.hpp"

namespace mpc
{
op::SecondOrderConeProgram buildSCOP(
    Model &model,
    Eigen::MatrixXd &X,
    Eigen::MatrixXd &U,
    Model::state_vector_t &state_weights,
    Model::input_vector_t &input_weights,
    Model::state_vector_t &x_init,
    Model::state_vector_t &x_des,
    Model::state_matrix_t &A,
    Model::control_matrix_t &B);
}