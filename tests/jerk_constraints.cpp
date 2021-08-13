#include <gsplines++/BasisLegendre.hpp>
#include <gsplines++/Collocation/GaussLobattoPointsWeights.hpp>
#include <gsplines++/Functions/ElementalFunctions.hpp>
#include <gsplines++/ipopt_solver.hpp>
#include <ifopt/ipopt_solver.h>
#include <ifopt/problem.h>
#include <opstop/jerk_constraints.hpp>

#include <opstop/differ.hpp>

#include <fenv.h>
#include <iostream>

#include <opstop/parametrization.hpp>

using namespace opstop;

std::size_t number_of_wp = 3;
std::size_t codom_dim = 7;
std::size_t nglp = 10;

double exec_time = (double)number_of_wp - 1.0;
double T = exec_time * 0.9;
double ti = 0.5 * exec_time;
double Ts = 0.8 * T - ti;
double sf = ti * (Ts / ti + 1);

double eta_0 = 1.0 / 15.0 * (8.0 * exec_time / ti - 7.0);
double xi_0 = (exec_time / ti - 1.0);

double sf_radius = (exec_time / ti - eta_0) * ti / 2;
double sf_center = ((exec_time / ti - eta_0) / 2.0 + eta_0) * ti;
double Ts_center = xi_0 / 2.0 * ti;
double Ts_radius = xi_0 / 2.0 * ti;

Eigen::MatrixXd wp(Eigen::MatrixXd::Random(number_of_wp, codom_dim));

gsplines::PiecewiseFunction trj = gsplines_opt::optimal_sobolev_norm(
    wp, gsplines::basis::BasisLegendre(6), {{4, 1}}, exec_time);

Eigen::VectorXd pol_coeff(6);

void compare_assert(Eigen::VectorXd &_m_nom, Eigen::VectorXd &_m_test) {

  std::cout << "\n-- norm of the differenc--\n";
  std::cout << (_m_nom - _m_test).norm() << "\n----\n";
  if (_m_nom.array().abs().maxCoeff() < 1.0e-9) {
    assert((_m_nom - _m_test).array().abs().maxCoeff() < 1.0e-9);
  } else {
    double err = (_m_nom - _m_test).array().abs().maxCoeff() /
                 _m_nom.array().abs().maxCoeff();

    assert(err < 1.0e-9);
  }
}
int main() {
  feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);

  std::vector<double> tb(7, 10.0);
  JerkConstraints cnstrt(trj, nglp, ti, tb);

  Eigen::VectorXd glp(nglp);

  std::tie(glp, std::ignore) =
      gsplines::collocation::legendre_gauss_lobatto_points_and_weights(nglp);

  gsplines::functions::FunctionExpression tau_par_inv =
      Ts_center * gsplines::functions::Identity({0.0, 1.0}) +
      gsplines::functions::ConstFunction({0.0, 1.0}, 1, ti);

  gsplines::functions::FunctionExpression diffeo =
      opstop::get_diffeo(ti, Ts_center, sf_center);

  gsplines::functions::FunctionExpression trj_stop = trj.compose(diffeo);

  gsplines::functions::FunctionExpression jerk =
      trj_stop.derivate(3).compose(tau_par_inv);
  gsplines::functions::FunctionExpression jerk_2 = trj_stop.derivate(3);

  Eigen::VectorXd glp_in_ti_T = (cnstrt.get_glp() * Ts_center).array() + ti;

  Eigen::Vector2d x;
  x(0) = Ts_center;
  x(1) = sf_center;

  Eigen::VectorXd val_test = cnstrt.__GetValues(x);
  Eigen::VectorXd val_nom = Eigen::Map<const Eigen::VectorXd>(
      jerk(cnstrt.get_glp()).data(), val_test.size());
  Eigen::VectorXd val_nom_2 = Eigen::Map<const Eigen::VectorXd>(
      jerk_2(glp_in_ti_T).data(), val_test.size());
  compare_assert(val_nom, val_test);

  Eigen::MatrixXd jac_test = cnstrt.__GetJacobian(x);

  Eigen::MatrixXd jac_nom(jac_test);
  jac_nom.setConstant(0.0);

  std::size_t precision_order = 12;
  std::size_t diff_order = 1;
  Eigen::VectorXd diff_coeff(precision_order + diff_order);
  Eigen::VectorXd diff_eval_points(precision_order + diff_order);
  differ_central(1.0e-6, diff_order, precision_order, diff_coeff.data(),
                 diff_eval_points.data());

  for (std::size_t uici = 0; uici < precision_order + diff_order; uici++) {
    x(0) = Ts_center + diff_eval_points(uici);
    jac_nom.col(0) += diff_coeff(uici) * cnstrt.__GetValues(x);
  }

  x(0) = Ts_center;
  for (std::size_t uici = 0; uici < precision_order + diff_order; uici++) {
    x(1) = sf_center + diff_eval_points(uici);
    jac_nom.col(1) += diff_coeff(uici) * cnstrt.__GetValues(x);
  }

  std::cout << "\n ----- \n" << jac_test.col(0) << "\n ----- \n";

  Eigen::MatrixXd err_mat = (jac_nom - jac_test).array().abs().matrix();

  double jac_nom_inf_norm = jac_nom.array().abs().maxCoeff();

  double err_inf_norm = err_mat.array().abs().maxCoeff();

  if (jac_nom_inf_norm < 1.0e-6) {
    assert(err_inf_norm < 5.0e-7);
  } else {

    assert(err_inf_norm / jac_nom_inf_norm < 5.0e-7);
  }

  return 0;
}
