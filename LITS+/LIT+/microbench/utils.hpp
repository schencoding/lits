#pragma once

// ------------------------------------------------------------
// Fast Zipf sampler
// ------------------------------------------------------------
static size_t zipf_idx(std::mt19937_64 &rng, size_t n, double alpha = 1.2)
{
    static thread_local std::uniform_real_distribution<double> unif(0.0, 1.0);
    double b = pow(2.0, alpha - 1.0);
    while (true)
    {
        double u = unif(rng), v = unif(rng);
        double x = floor(pow(u, -1.0 / (alpha - 1.0)));
        if (x < 1.0 || x > n)
            continue;
        double t = pow(1.0 + 1.0 / x, alpha - 1.0);
        if (v * x * (t - 1) / (b - 1) <= 1.0)
            return static_cast<size_t>(x - 1);
    }
}