// NEAR(value, expected, tolerance): |value - expected| < tolerance (doctest::Approx has no absolute margin in 2.4.11)
#pragma once
#include <cmath>
#define NEAR(v, e, tol) CHECK(std::abs((v) - (e)) < (tol))
