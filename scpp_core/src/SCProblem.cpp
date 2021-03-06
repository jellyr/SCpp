#include "optimizationProblem.hpp"
#include "SCProblem.hpp"

namespace scpp
{

op::SecondOrderConeProgram buildSCProblem(
    double &weight_time,
    double &weight_trust_region_time,
    double &weight_trust_region_trajectory,
    double &weight_virtual_control,
    trajectory_data_t &td,
    discretization_data_t &dd)
{
    const size_t K = td.n_X();

    op::SecondOrderConeProgram socp;

    op::Variable v_X = socp.createVariable("X", Model::state_dim, td.n_X());                   // states
    op::Variable v_U = socp.createVariable("U", Model::input_dim, td.n_U());                   // inputs
    op::Variable v_nu = socp.createVariable("nu", Model::state_dim, td.n_X() - 1);             // virtual control
    op::Variable v_nu_bound = socp.createVariable("nu_bound", Model::state_dim, td.n_X() - 1); // virtual control
    op::Variable v_norm1_nu = socp.createVariable("norm1_nu");                                 // virtual control norm upper bound
    op::Variable v_delta = socp.createVariable("delta", K);                                    // change of the stacked [ x(k), u(k) ] vector
    if (dd.variableTime())
    {
        socp.createVariable("sigma");       // total time
        socp.createVariable("delta_sigma"); // squared change of sigma
        // minimize total time
        socp.addMinimizationTerm(op::Parameter(&weight_time) * socp.getVariable("sigma"));
        // Total time must not be negative
        socp.addConstraint(socp.getVariable("sigma") >= op::Parameter(0.001));
    }

    for (size_t k = 0; k < K - 1; k++)
    {
        /**
         * Build linearized model equality constraint
         *    x(k+1) == A x(k) + B u(k) + C u(k+1) + Sigma sigma + z + nu
         * 
         */
        op::Affine lhs = op::Parameter(&dd.A.at(k)) * v_X.col(k) +
                         op::Parameter(&dd.B.at(k)) * v_U.col(k) +
                         op::Parameter(&dd.z.at(k)) +
                         v_nu.col(k);

        if (dd.interpolatedInput())
        {
            lhs += op::Parameter(&dd.C.at(k)) * v_U.col(k + 1);
        }
        if (dd.variableTime())
        {
            lhs += op::Parameter(&dd.s.at(k)) * socp.getVariable("sigma");
        }

        socp.addConstraint(lhs == v_X.col(k + 1));
    }

    /**
     * Build virtual control norm
     *
     * minimize (weight_virtual_control * norm1_nu)
     * s.t. sum(nu_bound) <= norm1_nu
     *      -nu_bound <= nu <= nu_bound
     *
     */
    {
        socp.addConstraint(v_nu >= -v_nu_bound);
        socp.addConstraint(v_nu_bound >= v_nu);

        op::Affine bound_sum = op::sum(v_nu_bound);

        // sum(nu_bound) <= norm1_nu
        socp.addConstraint(v_norm1_nu >= bound_sum);

        // Minimize the virtual control
        socp.addMinimizationTerm(op::Parameter(&weight_virtual_control) * v_norm1_nu);
    }

    if (dd.variableTime())
    {
        /**
        *  Build sigma trust region
        * (sigma - sigma0) * (sigma - sigma0) <= delta_sigma
        *          is equivalent to
        * norm2(
        *        0.5 - 0.5 * delta_sigma
        *        sigma0 - sigma
        *      )
        *      <= 0.5 + 0.5 * delta_sigma;
        */
        {
            op::Affine norm2_args = op::vstack({op::Parameter(0.5) + op::Parameter(-0.5) * socp.getVariable("delta_sigma"),
                                                -op::Parameter(&td.t) + socp.getVariable("sigma")});

            socp.addConstraint(op::norm2(norm2_args) <= op::Parameter(0.5) + op::Parameter(0.5) * socp.getVariable("delta_sigma"));

            // Minimize delta_sigma
            socp.addMinimizationTerm(op::Parameter(&weight_trust_region_time) * socp.getVariable("delta_sigma"));
        }
    }

    for (size_t k = 0; k < K; k++)
    {
        /**
         * Build state and input trust-region:
         *     (x - x0)^T * (x - x0)  +  (u - u0)^T * (u - u0)  <=  delta
         *
         * The constraint is equivalent to the SOCP form:
         *
         * norm2(
         *        0.5 - 0.5 * delta
         *        [(x - x0)^T | (u - u0)^T]^T
         *      )
         *     <= 0.5 + 0.5 * delta;
         *
         */

        op::Affine norm2_args = op::vstack({op::Parameter(0.5) + op::Parameter(-0.5) * v_delta(k),
                                            op::Parameter(&td.X.at(k)) + -v_X.col(k)});

        if (dd.interpolatedInput() or (not dd.interpolatedInput() and k < K - 1))
        {
            norm2_args = op::vstack({norm2_args,
                                     op::Parameter(&td.U.at(k)) + -v_U.col(k)});
        }

        socp.addConstraint(op::norm2(norm2_args) <= op::Parameter(0.5) + op::Parameter(0.5) * v_delta(k));
    }

    /**
     * Minimize combined state/input trust region over all K:
     * 
     */
    {
        // Minimize trust region cost
        socp.addMinimizationTerm(op::Parameter(&weight_trust_region_trajectory) * op::sum(v_delta));
    }

    return socp;
}

} // namespace scpp
