import numpy as np
from scipy.stats import norm
import math

def calculate_power(p1, p2, n1, n2, alpha=0.05):
    p_pool = (p1*n1 + p2*n2) / (n1 + n2)
    se_null = np.sqrt(p_pool * (1 - p_pool) * (1/n1 + 1/n2))
    
    z_alpha = norm.ppf(1 - alpha)
    critical_diff = z_alpha * se_null
    
    se_alt = np.sqrt(p1*(1-p1)/n1 + p2*(1-p2)/n2)
    true_diff = p1 - p2
    
    z_beta = (critical_diff - true_diff) / se_alt
    power = 1 - norm.cdf(z_beta)
    return power

def calculate_sample_size(p1, p2, alpha=0.05, power=0.90):
    p_pool = (p1 + p2) / 2
    z_alpha = norm.ppf(1 - alpha)
    z_beta = norm.ppf(power)
    
    numerator = (z_alpha * np.sqrt(2 * p_pool * (1 - p_pool)) + \
                 z_beta * np.sqrt(p1*(1-p1) + p2*(1-p2))) ** 2
    denominator = (p1 - p2) ** 2
    
    n_exact = numerator / denominator
    return n_exact, math.ceil(n_exact)

power_b = calculate_power(p1=0.35, p2=0.25, n1=100, n2=100, alpha=0.05)
print(f"Power: {power_b:.4f}")

n_exact, n_final = calculate_sample_size(p1=0.40, p2=0.25, alpha=0.05, power=0.90)
print(f"Exact Sample Size: {n_exact:.4f}")
print(f"Final Sample Size: {n_final}")