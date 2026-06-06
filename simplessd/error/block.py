import pandas as pd
import numpy as np
from scipy import stats
import random

u1 = 0  # Mean of the first Gaussian distribution
sigma1 = 0.3  # Standard deviation of the first Gaussian distribution

x = np.arange(-1.152 * 2, 1.152 * 2, 0.002)
# First Gaussian distribution PDF
y1 = np.multiply(np.power(np.sqrt(2 * np.pi) * sigma1, -1), np.exp(-np.power(x - u1, 2) / 2 * sigma1 ** 2))

y1 = y1 / max(y1)

df = pd.DataFrame(y1)
df.to_csv('block_difference.csv')

