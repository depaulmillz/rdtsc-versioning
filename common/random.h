/**
 * Preliminary C++ implementation of binary search tree using LLX/SCX.
 * 
 * Copyright (C) 2014 Trevor Brown
 * 
 */

#include <cfloat>
#include <cmath>

#ifndef RANDOM_H
#define	RANDOM_H

inline float zeta(float theta, int n) {
  float sum = 0;
  for (int i = 1; i <= n; i++) {
      sum += 1 / (std::pow(i, theta));
  }
  return sum;
}

class Random {
private:
    unsigned int seed;

    float theta;
    float zetaN;
    float alpha;
    float eta;

public:
    Random(void) {
        this->seed = 0;
    }
    Random(int seed) {
        this->seed = seed;
    }
    
    void setSeed(int seed) {
        this->seed = seed;
    }

    void setTheta(int n, float theta_) {
      theta = theta_;
      zetaN = zeta(theta, n + 1);
      alpha = 1.0f / (1.0f - theta);

      eta = (1.0f - powf(2.0f / (n + 1.0f), 1.0f - theta)) 
            / (1.0f - zeta(theta, 2) / zetaN);
    }

    int nextZipf(int n) {
      float u = nextNatural(1000000) / 1000000.0f;
      float uz = u * zetaN;

      if(uz < 1) return 0;
      if(uz < 1 + powf(0.5f, theta)) return 1;
      return (n + 1.0f) * powf(eta * u - eta + 1.0f, alpha);
    }

    /** returns pseudorandom x satisfying 0 <= x < n. **/
    int nextNatural(int n) {
        seed ^= seed << 6;
        seed ^= seed >> 21;
        seed ^= seed << 7;
        int retval = (int) (seed % n);
        return (retval < 0 ? -retval : retval);
    }

    /** returns pseudorandom x satisfying 0 <= x < n. **/
    unsigned int nextNatural() {
        seed ^= seed << 6;
        seed ^= seed >> 21;
        seed ^= seed << 7;
        return seed;
    }

};

#endif	/* RANDOM_H */

