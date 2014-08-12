#include "Ancillaries.h"
#include "DataStructures.h"
#include "AbstractState.h"

#if defined(ENABLE_CATCH)

#include "crossplatform_shared_ptr.h"
#include "catch.hpp"

#endif

namespace CoolProp{

SaturationAncillaryFunction::SaturationAncillaryFunction(rapidjson::Value &json_code)
{
	std::string type = cpjson::get_string(json_code,"type");
	if (!type.compare("rational_polynomial"))
	{
		num_coeffs = vec_to_eigen(cpjson::get_double_array(json_code["A"]));
		den_coeffs = vec_to_eigen(cpjson::get_double_array(json_code["B"]));
		max_abs_error = cpjson::get_double(json_code,"max_abs_error");
	}
	else
	{
		n = cpjson::get_double_array(json_code["n"]);
		t = cpjson::get_double_array(json_code["t"]);
		Tmin = cpjson::get_double(json_code,"Tmin");
		Tmax = cpjson::get_double(json_code,"Tmax");
		reducing_value = cpjson::get_double(json_code,"reducing_value");
		using_tau_r = cpjson::get_bool(json_code,"using_tau_r");
		T_r = cpjson::get_double(json_code,"T_r");    
	}   
	
	if (!type.compare("rational_polynomial"))
		this->type = TYPE_RATIONAL_POLYNOMIAL;
	else if (!type.compare("rhoLnoexp"))
		this->type = TYPE_NOT_EXPONENTIAL;
	else
		this->type = TYPE_EXPONENTIAL;
	this->N = n.size();
	s = n;
};
    
double SaturationAncillaryFunction::evaluate(double T)
{
	if (type == TYPE_NOT_SET)
	{
		throw ValueError(format("type not set"));
	}
	else if (type == TYPE_RATIONAL_POLYNOMIAL)
	{
		Polynomial2D poly;
		return poly.evaluate(num_coeffs, T)/poly.evaluate(den_coeffs, T);
	}
	else
	{
		double THETA = 1-T/T_r;

		for (std::size_t i = 0; i < N; ++i)
		{
			s[i] = n[i]*pow(THETA, t[i]);
		}
		double summer = std::accumulate(s.begin(), s.end(), 0.0);

		if (type == TYPE_NOT_EXPONENTIAL)
		{
			return reducing_value*(1+summer);
		}
		else
		{
			double tau_r_value;
			if (using_tau_r)
				tau_r_value = T_r/T;
			else
				tau_r_value = 1.0;
			return reducing_value*exp(tau_r_value*summer);
		}
	}
}
double SaturationAncillaryFunction::invert(double value)
{
	// Invert the ancillary curve to get the temperature as a function of the output variable
	// Define the residual to be driven to zero
	class solver_resid : public FuncWrapper1D
	{
	public:
		int other;
		SaturationAncillaryFunction *anc;
		long double T, value, r, current_value;

		solver_resid(SaturationAncillaryFunction *anc, long double value) : anc(anc), value(value){};

		double call(double T){
			this->T = T;
			current_value = anc->evaluate(T);
			r = current_value - value;
			return r;
		};
	};
	solver_resid resid(this, value);
	std::string errstring;

	try{
		return Brent(resid,Tmin,Tmax,DBL_EPSILON,1e-12,100,errstring);
	}
	catch(std::exception &e){
		return Secant(resid,Tmax, -0.01, 1e-12, 100, errstring);
	}
}

void MeltingLineVariables::set_limits(void)
{
	if (type == MELTING_LINE_SIMON_TYPE){
		
        // Fill in the min and max pressures for each part
        for (std::size_t i = 0; i < simon.parts.size(); ++i){
            MeltingLinePiecewiseSimonSegment &part = simon.parts[i];
            part.p_min = part.p_0 + part.a*(pow(part.T_min/part.T_0,part.c)-1);
            part.p_max = part.p_0 + part.a*(pow(part.T_max/part.T_0,part.c)-1);
        }
        pmin = simon.parts.front().p_min;
		pmax = simon.parts.back().p_max;
        Tmin = simon.parts.front().T_min;
		Tmax = simon.parts.back().T_max;
	}
	else if (type == MELTING_LINE_POLYNOMIAL_IN_TR_TYPE){
        // Fill in the min and max pressures for each part
        for (std::size_t i = 0; i < polynomial_in_Tr.parts.size(); ++i){
            MeltingLinePiecewisePolynomialInTrSegment &part = polynomial_in_Tr.parts[i];
            part.p_min = part.evaluate(part.T_min);
            part.p_max = part.evaluate(part.T_max);
        }
        Tmin = polynomial_in_Tr.parts.front().T_min;
        pmin = polynomial_in_Tr.parts.front().p_min;
		Tmax = polynomial_in_Tr.parts.back().T_max;
        pmax = polynomial_in_Tr.parts.back().p_max;
	}
	else if (type == MELTING_LINE_POLYNOMIAL_IN_THETA_TYPE){
		MeltingLinePiecewisePolynomialInThetaSegment &partmin = polynomial_in_Theta.parts[0];
		MeltingLinePiecewisePolynomialInThetaSegment &partmax = polynomial_in_Theta.parts[polynomial_in_Theta.parts.size()-1];
		Tmin = partmin.T_0;
		Tmax = partmax.T_max;
		pmin = partmin.p_0;
		//pmax = evaluate(iP, iT, Tmax);
	}
	else{
		throw ValueError("only Simon supported now");
	}
}

long double MeltingLineVariables::evaluate(int OF, int GIVEN, long double value)
{
	if (type == MELTING_LINE_NOT_SET){throw ValueError("Melting line curve not set");}
	if (OF == iP && GIVEN == iT){
		long double T = value;
		if (type == MELTING_LINE_SIMON_TYPE){
			// Need to find the right segment
			for (std::size_t i = 0; i < simon.parts.size(); ++i){
				MeltingLinePiecewiseSimonSegment &part = simon.parts[i];
				if (is_in_closed_range(part.T_min, part.T_max, T)){
					return part.p_0 + part.a*(pow(T/part.T_0,part.c)-1);
				}
			}
			throw ValueError("unable to calculate melting line (p,T) for Simon curve");
		}
		else if (type == MELTING_LINE_POLYNOMIAL_IN_TR_TYPE){
			// Need to find the right segment
			for (std::size_t i = 0; i < polynomial_in_Tr.parts.size(); ++i){
				MeltingLinePiecewisePolynomialInTrSegment &part = polynomial_in_Tr.parts[i];
				if (is_in_closed_range(part.T_min, part.T_max, T)){
					return part.evaluate(T);
				}
			}
			throw ValueError("unable to calculate melting line (p,T) for polynomial_in_Tr curve");
		}
		else if (type == MELTING_LINE_POLYNOMIAL_IN_THETA_TYPE){
			// Need to find the right segment
			for (std::size_t i = 0; i < polynomial_in_Theta.parts.size(); ++i){
				MeltingLinePiecewisePolynomialInThetaSegment &part = polynomial_in_Theta.parts[i];
				if (is_in_closed_range(part.T_min, part.T_max, T)){
					long double summer = 0;
					for (std::size_t i =0; i < part.a.size(); ++i){
						summer += part.a[i]*pow(T/part.T_0-1,part.t[i]);
					}
					return part.p_0*(1+summer);
				}
			}
			throw ValueError("unable to calculate melting line (p,T) for polynomial_in_Theta curve");
		}
		else{
			throw ValueError(format("Invalid melting line type [%d]",type));
		}
	}
	else{
		if (type == MELTING_LINE_SIMON_TYPE){
			// Need to find the right segment
			for (std::size_t i = 0; i < simon.parts.size(); ++i){
				MeltingLinePiecewiseSimonSegment &part = simon.parts[i];
				//  p = part.p_0 + part.a*(pow(T/part.T_0,part.c)-1);
				long double T = pow((value-part.p_0)/part.a+1,1/part.c)*part.T_0;
				if (T >= part.T_0 && T <= part.T_max){
					return T;
				}
			}
			throw ValueError("unable to calculate melting line p(T) for Simon curve");
		}
		else if (type == MELTING_LINE_POLYNOMIAL_IN_TR_TYPE)
		{
            class solver_resid : public FuncWrapper1D
			{
			public:
				MeltingLinePiecewisePolynomialInTrSegment *part;
				long double r, given_p, calc_p, T;
				solver_resid(MeltingLinePiecewisePolynomialInTrSegment *part, long double p) : part(part), given_p(p){};
				double call(double T){

					this->T = T;

					calc_p = part->evaluate(T);

					// Difference between the two is to be driven to zero
					r = given_p - calc_p;

					return r;
				};
			};

			// Need to find the right segment
			for (std::size_t i = 0; i < polynomial_in_Tr.parts.size(); ++i){
                MeltingLinePiecewisePolynomialInTrSegment &part = polynomial_in_Tr.parts[i];
                if (is_in_closed_range(part.p_min, part.p_max, value)){
                    std::string errstr;
                    solver_resid resid(&part, value);
                    double T = Brent(resid, part.T_min, part.T_max, DBL_EPSILON, 1e-12, 100, errstr);
                    return T;
                }
            }
            throw ValueError("unable to calculate melting line T(p) for polynomial_in_Tr curve");
		}
		else{
			throw ValueError(format("Invalid melting line type T(p) [%d]",type));
		}
	}
}

}; /* namespace CoolProp */

#if defined(ENABLE_CATCH)
TEST_CASE("Water melting line", "")
{
    shared_ptr<CoolProp::AbstractState> AS(CoolProp::AbstractState::factory("HEOS","water"));
    int iT = CoolProp::iT, iP = CoolProp::iP;
    SECTION("Ice Ih-liquid")
    {
		double actual = AS->melting_line(iT, iP, 138.268e6);
        double expected = 260.0;
        CAPTURE(actual);
        CAPTURE(expected);
        CHECK(std::abs(actual-expected) < 0.01);
    }
    SECTION("Ice III-liquid")
    {
		double actual = AS->melting_line(iT, iP, 268.685e6);
        double expected = 254;
        CAPTURE(actual);
        CAPTURE(expected);
        CHECK(std::abs(actual-expected) < 0.01);
    }
    SECTION("Ice V-liquid")
    {
		double actual = AS->melting_line(iT, iP, 479.640e6);
        double expected = 265;
        CAPTURE(actual);
        CAPTURE(expected);
        CHECK(std::abs(actual-expected) < 0.01);
    }
    SECTION("Ice VI-liquid")
    {
		double actual = AS->melting_line(iT, iP, 1356.76e6);
        double expected = 320;
        CAPTURE(actual);
        CAPTURE(expected);
        CHECK(std::abs(actual-expected) < 1);
    }
}
#endif