#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_cdf.h>
#include <cmath>
#include <omp.h>
#include <chrono>

using namespace std;
    


void init_ran(gsl_rng * &r, unsigned long s){
    const gsl_rng_type * T;
    gsl_rng_env_setup();
    T = gsl_rng_default;
    r = gsl_rng_alloc(T);
    gsl_rng_set(r, s);
}



int get_max_gamma(double alpha, int K, double thr){
    int max_gamma = 0;
    double cdf_Q = gsl_cdf_poisson_Q(max_gamma, alpha * K);
    while (cdf_Q > thr){
        max_gamma++;
        cdf_Q = gsl_cdf_poisson_Q(max_gamma, alpha * K);
    }
    return max_gamma;
}


// initializes all the joint and conditional probabilities
// prob_joint[ch][i], with ch=0,...,2^{K}-1 and i=0,...,2^{K}-1. The index ch is the 
// unsat combination and i is the combination of the spins
// pu_cond[ch][j][S], with ch=0,...,2^{K}-1, j=0,...,K-1 and S=0,1
void init_probs(double ***&prob_joint, double ***&pu_cond, double **&pi, 
                double ***&me_sum, double **&prob_joint_av,  
                double alpha, int K, int nch_fn, double p0, long pop_size, int ***&gamma_vals, gsl_rng * r){
    double prod;
    int bit;
    prob_joint = new double **[nch_fn];
    pu_cond = new double **[nch_fn];
    me_sum = new double **[nch_fn];
    gamma_vals = new int **[nch_fn];
    prob_joint_av = new double *[nch_fn];
    for (int ch_u = 0; ch_u < nch_fn; ch_u++){
        prob_joint_av[ch_u] = new double [nch_fn]; 
        prob_joint[ch_u] = new double *[pop_size];
        me_sum[ch_u] = new double *[pop_size];
        gamma_vals[ch_u] = new int *[pop_size];
        for (long elem = 0; elem < pop_size; elem++){
            prob_joint[ch_u][elem] = new double [nch_fn];
            me_sum[ch_u][elem] = new double [nch_fn];
            gamma_vals[ch_u][elem] = new int [K];
            for (int ch = 0; ch < nch_fn; ch++){
                prod = 1;
                for (int w = 0; w < K; w++){
                    bit = ((ch >> w) & 1);
                    prod *= (bit + (1 - 2 * bit) * p0);
                }
                prob_joint[ch_u][elem][ch] = prod;
            }

            for (int w = 0; w < K ; w++){
                gamma_vals[ch_u][elem][w] = gsl_ran_poisson(r, alpha * K);
            }
        }
        pu_cond[ch_u] = new double*[K];
        for (int w = 0; w < K; w++){
            pu_cond[ch_u][w] = new double [2];
        }
    }

    pi = new double*[K];
    for (int w = 0; w < K; w++){
        pi[w] = new double[2];
    }
}


// initializes the auxiliary arrays for the Runge-Kutta integration
void init_RK_arr(double ***&k1, double ***&k2, double ***&prob_joint_1, 
                 int nch_fn, long pop_size){
    k1 = new double **[nch_fn];
    k2 = new double **[nch_fn];
    prob_joint_1 = new double **[nch_fn];
    for (int ch_u = 0; ch_u < nch_fn; ch_u++){
        k1[ch_u] = new double *[pop_size];
        k2[ch_u] = new double *[pop_size];
        prob_joint_1[ch_u] = new double *[pop_size];
        for (long elem = 0; elem < pop_size; elem++){
            k1[ch_u][elem] = new double [nch_fn];
            k2[ch_u][elem] = new double [nch_fn];
            prob_joint_1[ch_u][elem] = new double [nch_fn];
            for (int ch = 0; ch < nch_fn; ch++){
                k1[ch_u][elem][ch] = 0;
                k2[ch_u][elem][ch] = 0;
                prob_joint_1[ch_u][elem][ch] = 0;
            }
        }
    }
}


// rate of the Focused Metropolis Search algorithm.
double rate_fms(int E0, int E1, int K, double eta){
    double dE = E1 - E0;
    if (dE > 0){
        return double(E0) / K * pow(eta, dE);
    }else{
        return double(E0) / K;
    }
}


void table_all_rates(int max_gamma, int K, double eta, double **&rates){
    rates = new double *[max_gamma + 1];
    for (int E0 = 0; E0 < max_gamma + 1; E0++){
        rates[E0] = new double [max_gamma + 1];
        for (int E1 = 0; E1 < max_gamma + 1; E1++){
            rates[E0][E1] = rate_fms(E0, E1, K, eta);
        }
    }
}


double rate_fms(int E0, int E1, double **rates, double e_av){
    return rates[E0][E1] / e_av;
}


void average_probs(double ***prob_joint, double **prob_joint_av, int nch_fn, long pop_size){    
    for (int ch_u = 0; ch_u < nch_fn; ch_u++){
        for (int ch = 0; ch < nch_fn; ch++){
            prob_joint_av[ch_u][ch] = 0;
            for (long elem = 0; elem < pop_size; elem++){
                prob_joint_av[ch_u][ch] += prob_joint[ch_u][elem][ch];
            }
            prob_joint_av[ch_u][ch] /= pop_size;
        }
    }
}


// it computes the conditional probabilities of having a partially unsatisfied clause, given the 
// value of one variable in the clause
void comp_pcond(double **prob_joint_av, double ***pu_cond, double **pi, 
                int K, int nch_fn){
    double pu;
    int bit;
    int ch_uns_flip;
    for (int ch_u = 0; ch_u < nch_fn; ch_u++){
        for (int w = 0; w < K; w++){
            for (int s = 0; s < 2; s++){
                pi[w][s] = 0;
            }
        }

        for (int ch = 0; ch < nch_fn; ch++){
            for (int w = 0; w < K; w++){
                bit = ((ch >> w) & 1);
                pi[w][bit] += prob_joint_av[ch_u][ch];
            }
        }

        for (int w = 0; w < K; w++){
            bit = ((ch_u >> w) & 1); 
            ch_uns_flip = (ch_u ^ (1 << w));
            pu_cond[ch_u][w][bit] = prob_joint_av[ch_u][ch_u] / pi[w][bit];
            pu_cond[ch_u][w][1 - bit] = prob_joint_av[ch_u][ch_uns_flip] / pi[w][1 - bit];
        }
    }
}


// it gets the conditional probabilities for the factor nodes unsatisfied by
// si=1 (pu_l[0]) and the ones unsatisfied by si=-1 (pu_l[1])
// the second index is the value of the spin in the conditional
void get_pu_l(double ***pu_cond, double ***pu_l, int K, int gamma, int nch_fn, gsl_rng * r,
              int &lp, int &ln){
    int ch_u, cond_spin, bit;
    lp = 0, ln = 0;
    for (int i = 0; i < gamma; i++){
        ch_u = gsl_rng_uniform_int(r, nch_fn);
        cond_spin = gsl_rng_uniform_int(r, K);
        bit = ((ch_u >> cond_spin) & 1);
        if (bit == 0){
            pu_l[0][0][ln] = pu_cond[ch_u][cond_spin][0];
            pu_l[0][1][ln] = pu_cond[ch_u][cond_spin][1];
            ln++;
        }else{
            pu_l[1][0][lp] = pu_cond[ch_u][cond_spin][0];
            pu_l[1][1][lp] = pu_cond[ch_u][cond_spin][1];
            lp++;
        }
    }
}


// This function recursively computes a vector fE[k], with k=0,..., c
// fE[k] is the sum of 'c' binary variable constrained to sum exactly 'k' and weighted
// with a factorized distribution pu[i], with i = 0,..., c-1
void recursive_marginal(double *pu, int c, int k, double *fE, double *fEnew){
    if (k < c){
        fEnew[0] = (1 - pu[k]) * fE[0];
        for (int i = 0; i < k; i++){
            fEnew[i + 1] = (1 - pu[k]) * fE[i + 1] + pu[k] * fE[i];
        }
        fEnew[k + 1] = pu[k] * fE[k];
        recursive_marginal(pu, c, k + 1, fEnew, fE);
        // it inverts the order of fEnew and fE so that in the new call the latest
        // info is saved in the inner variable fE
    }else{
        for (int i = 0; i < c + 1; i++){
            fEnew[i] = fE[i];
            // it makes sure that both arrays contain the latest values
        }
    }
}


void init_aux_arr(double ***&pu_l, double ***&fE, double *&fEnew, int gamma){
    fEnew = new double [gamma + 1];

    pu_l = new double **[2];  // the first index is used to distinguish the factor nodes
    // that are unsatisfied when the spin is 1 or when the spin is -1. This means that pu_l[0]
    // contains the probabilities of the factor nodes where the spin enters with link li=-1.
    // Those are unsatisfied when the spin si=1. Analogously, pu_l[1] corresponds to li=1 and 
    // factor nodes unsatisfied by si=-1. 
    fE = new double **[2];
    for (int s = 0; s < 2; s++){
        fE[s] = new double *[2];
        pu_l[s] = new double *[2];
        // the second index goes for the spin in the conditional of the probabilities.
        for (int si = 0; si < 2; si++){
            pu_l[s][si] = new double [gamma];
            fE[s][si] = new double [gamma + 1];
        }
    }
}



void delete_aux_arr(double ***&pu_l, double ***&fE, double *&fEnew){
    delete [] fEnew;
    for (int s = 0; s < 2; s++){
        for (int si = 0; si < 2; si++){
            delete [] pu_l[s][si];
            delete [] fE[s][si];
        }
        delete [] fE[s];
        delete [] pu_l[s];
    }
    delete [] pu_l;
    delete [] fE;
}


// it does the sum in the derivative of the CDA equations
// fn_src is the origin factor node where one is computing the derivative
// part_uns is 1 if the other variables in fn_src are partially
// unsatisfying their links, and is 0 otherwise. 
void sum_fms(int K, int gamma, int ch_u, int plc_he, double *prob_joint, double ***pu_cond, 
             double **rates, int nch_fn, double e_av, double *me_sum_src, gsl_rng * r){

    double ***pu_l, ***fE, *fEnew;
    init_aux_arr(pu_l, fE, fEnew, gamma);
    
    int lp, ln;
    get_pu_l(pu_cond, pu_l, K, gamma, nch_fn, r, lp, ln);
    // remember that when l=1 the unsatisfying assingment is si=-1
    // therefore, ln corresponds to pu_l[0], and lp to pu_l[1]
    for (int s1 = 0; s1 < 2; s1++){
        for (int s2 = 0; s2 < 2; s2++){
            fE[s1][s2][0] = 1;
        }
    }
    recursive_marginal(pu_l[0][0], ln, 0, fE[0][0], fEnew);
    recursive_marginal(pu_l[0][1], ln, 0, fE[0][1], fEnew);
    recursive_marginal(pu_l[1][0], lp, 0, fE[1][0], fEnew);
    recursive_marginal(pu_l[1][1], lp, 0, fE[1][1], fEnew);

    double terms[2][2];
    int E[2];

    long he;
    int ch_flip;
    bool bit, uns, uns_flip;

    for (E[0] = 0; E[0] < ln + 1; E[0]++){
        for (E[1] = 0; E[1] < lp + 1; E[1]++){

            terms[0][0] = rate_fms(E[0], E[1], rates, e_av) * fE[0][0][E[0]] * fE[1][0][E[1]];
            terms[1][0] = rate_fms(E[1], E[0], rates, e_av) * fE[0][1][E[0]] * fE[1][1][E[1]];

            bit = ((ch_u >> plc_he) & 1);

            terms[bit][1] = rate_fms(E[bit] + 1, E[1 - bit], rates, e_av) * 
                            fE[bit][bit][E[bit]] * fE[1 - bit][bit][E[1 - bit]];
            terms[1 - bit][1] = rate_fms(E[1 - bit], E[bit] + 1, rates, e_av) * 
                                fE[1 - bit][1 - bit][E[1 - bit]] * fE[bit][1 - bit][E[bit]];
            
            for (int ch_src = 0; ch_src < nch_fn; ch_src++){
                bit = ((ch_src >> plc_he) & 1);
                ch_flip = (ch_src ^ (1 << plc_he));
                uns = (ch_src == ch_u);
                uns_flip = (ch_flip == ch_u);
                me_sum_src[ch_src] += -terms[bit][uns || uns_flip] * prob_joint[ch_src] + 
                                      terms[1 - bit][uns || uns_flip] * prob_joint[ch_flip];
                // if any of the two, uns and uns_flip, is one, then one has to use the terms
                // in terms[1]. One of them represents the probability of a jump when ch_src in unsat,
                // and therefore it goes from E[bit unsat] + 1 ----> E[bit sat]. The other jump makes
                // E[bit sat] ----> E[bit unsat] + 1
            }

        }
    }
    delete_aux_arr(pu_l, fE, fEnew);

}




// it computes all the derivatives of the joint probabilities
void der_fms(double ***prob_joint, double ***pu_cond, double **rates, long pop_size, 
             double alpha, int K, int max_gamma, int nch_fn, double e_av, double ***me_sum, 
             int ***gamma_vals, gsl_rng * r){
    for (int ch_u = 0; ch_u < nch_fn; ch_u++){
        for (long elem = 0; elem < pop_size; elem++){
            for (int ch = 0; ch < nch_fn; ch++){
                me_sum[ch_u][elem][ch] = 0;
            }
        }
    }

    // candidate to be a parallel for
    #pragma omp parallel for
    for (long elem = 0; elem < pop_size; elem++){
        for (int ch_u = 0; ch_u < nch_fn; ch_u++){
            for (int w = 0; w < K; w++){
                sum_fms(K, gamma_vals[ch_u][elem][w], ch_u, w, prob_joint[ch_u][elem],
                        pu_cond, rates, nch_fn, e_av, me_sum[ch_u][elem], r);
            }
        }
    }
}


double energy(double **prob_joint_av, int nch_fn){
    double e = 0;
    for (int ch_u = 0; ch_u < nch_fn; ch_u++){
        e += prob_joint_av[ch_u][ch_u];
    }
    return e / nch_fn;
}


// peforms the integration of the differential equations with the 2nd order Runge-Kutta
// the method is implemented with adaptive step size
void RK2_fms(double alpha, int K, int nch_fn, double eta, int max_gamma, long pop_size, 
             double p0, char *fileener, double tl, gsl_rng * r, double tol = 1e-2, double t0 = 0, double dt0 = 0.01, 
             double ef = 1e-6, double dt_min = 1e-7){
    double **rates;
    double ***prob_joint, ***pu_cond, ***me_sum, **pi, **prob_joint_av;
    double e, error;                 
    int ***gamma_vals;
    
    table_all_rates(max_gamma, K, eta, rates);
    
    init_probs(prob_joint, pu_cond, pi, me_sum, prob_joint_av, alpha, K, nch_fn, 
               p0, pop_size, gamma_vals, r);

    // initialize auxiliary arrays for the Runge-Kutta integration
    double ***k1, ***k2, ***prob_joint_1;
    init_RK_arr(k1, k2, prob_joint_1, nch_fn, pop_size);

    ofstream fe(fileener);
    average_probs(prob_joint, prob_joint_av, nch_fn, pop_size);

    e = energy(prob_joint_av, nch_fn) * alpha;
    fe << t0 << "\t" << e << endl;   // it prints the energy density

    double dt1 = dt0;
    double t = t0;

    bool valid;

    // the time scale is already given in Monte Carlo steps. Inside the rates I am using 
    // the energy density e_av
    while (t < tl){
        if (e < ef){
            //  cout << "Final energy reached" << endl;
            break;
        }

        auto t1 = std::chrono::high_resolution_clock::now();

        average_probs(prob_joint, prob_joint_av, nch_fn, pop_size);
        comp_pcond(prob_joint_av, pu_cond, pi, K, nch_fn);

        der_fms(prob_joint, pu_cond, rates, pop_size, alpha, K, max_gamma, nch_fn, e, me_sum, gamma_vals, r);   // in the rates, I use the energy density

        valid = true;
        for (int ch_u = 0; ch_u < nch_fn; ch_u++){
            for (long elem = 0; elem < pop_size; elem++){
                for (int ch = 0; ch < nch_fn; ch++){
                    k1[ch_u][elem][ch] = dt1 * me_sum[ch_u][elem][ch];
                    prob_joint_1[ch_u][elem][ch] = prob_joint[ch_u][elem][ch] + k1[ch_u][elem][ch];
                    if (prob_joint_1[ch_u][elem][ch] < 0){
                        valid = false;
                    }
                }
            }
        }

        while (!valid){
            //  cout << "joint probabilities became negative in the auxiliary step of RK2" << endl;
            dt1 /= 2;
            //  cout << "step divided by half    dt=" << dt1 << endl;
            if (dt1 < dt_min){
                dt_min /= 2;
                //  cout << "dt_min also halfed" << endl;
            }

            valid = true;
            for (int ch_u = 0; ch_u < nch_fn; ch_u++){
                for (long elem = 0; elem < pop_size; elem++){
                    for (int ch = 0; ch < nch_fn; ch++){
                        k1[ch_u][elem][ch] = dt1 * me_sum[ch_u][elem][ch];
                        prob_joint_1[ch_u][elem][ch] = prob_joint[ch_u][elem][ch] + k1[ch_u][elem][ch];
                        if (prob_joint_1[ch_u][elem][ch] < 0){
                            valid = false;
                        }
                    }
                }
            }
        }
        
        average_probs(prob_joint_1, prob_joint_av, nch_fn, pop_size);
        e = energy(prob_joint_av, nch_fn) * alpha;
        comp_pcond(prob_joint_av, pu_cond, pi, K, nch_fn);

        der_fms(prob_joint_1, pu_cond, rates, pop_size, alpha, K, max_gamma, nch_fn, e, me_sum, gamma_vals, r);
            
        valid = true;
        for (int ch_u = 0; ch_u < nch_fn; ch_u++){
            for (long elem = 0; elem < pop_size; elem++){
                for (int ch = 0; ch < nch_fn; ch++){
                    k2[ch_u][elem][ch] = dt1 * me_sum[ch_u][elem][ch];
                    if (prob_joint[ch_u][elem][ch] + (k1[ch_u][elem][ch] + k2[ch_u][elem][ch]) / 2 < 0){
                        valid = false;
                    }
                }
            }
        }

        if (!valid){
            //  cout << "Some probabilities would be negative if dt=" << dt1 << " is taken" << endl;
            dt1 /= 2;
            //  cout << "step divided by half    dt=" << dt1  << endl;
            if (dt1 < dt_min){
                dt_min /= 2;
                //  cout << "dt_min also halfed" << endl;
            }
            average_probs(prob_joint, prob_joint_av, nch_fn, pop_size);
            e = energy(prob_joint_av, nch_fn) * alpha;
        }else{
            error = 0;
            for (int ch_u = 0; ch_u < nch_fn; ch_u++){
                for (long elem = 0; elem < pop_size; elem++){
                    for (int ch = 0; ch < nch_fn; ch++){
                        error += fabs(k1[ch_u][elem][ch] - k2[ch_u][elem][ch]);
                    }
                }
            }

            error /= nch_fn * nch_fn * pop_size;

            if (error < 2 * tol){
                //  cout << "step dt=" << dt1 << "  accepted" << endl;
                //  cout << "error=" << error << endl;
                t += dt1;
                for (int ch_u = 0; ch_u < nch_fn; ch_u++){
                    for (long elem = 0; elem < pop_size; elem++){
                        for (int ch = 0; ch < nch_fn; ch++){
                            prob_joint[ch_u][elem][ch] += (k1[ch_u][elem][ch] + k2[ch_u][elem][ch]) / 2;
                        }
                    }
                }
                average_probs(prob_joint, prob_joint_av, nch_fn, pop_size);
                e = energy(prob_joint_av, nch_fn) * alpha;
                fe << t << "\t" << e << endl;

            }else{
                average_probs(prob_joint, prob_joint_av, nch_fn, pop_size);
                e = energy(prob_joint_av, nch_fn) * alpha;
                //  cout << "step dt=" << dt1 << "  rejected  new step will be attempted" << endl;
                //  cout << "error=" <<  error << endl;
            }

            dt1 = 4 * dt1 * sqrt(2 * tol / error) / 5;
            if(dt1 < dt_min){
                dt1 = dt_min;
            }

            //  cout << "Recommended step is dt=" << dt1 << endl;
        }

        auto t2 = std::chrono::high_resolution_clock::now();

        auto ms_int = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);

        cout << endl << "iteration time:   " << ms_int.count() << "ms" << endl; 
    }

    fe.close();

}


int main(int argc, char *argv[]) {
    long pop_size = atol(argv[1]);
    double alpha = atof(argv[2]);
    int K = atoi(argv[3]);
    unsigned long seed_r = atol(argv[4]);
    double eta = atof(argv[5]);
    double tl = atof(argv[6]);
    double tol = atof(argv[7]);
    int nthr = atoi(argv[8]);
    double eps_c = atof(argv[9]);

    int nch_fn = (1 << K);
    double p0 = 0.5;

    omp_set_num_threads(nthr);

    gsl_rng * r;
    init_ran(r, seed_r);

    char fileener[300]; 
    sprintf(fileener, "CDA1av_pop_gamma_FMS_ener_K_%d_alpha_%.4lf_eta_%.4lf_tl_%.2lf_seed_%li_tol_%.1e_popsize_%li_epsc_%.e.txt", 
            K, alpha, eta, tl, seed_r, tol, pop_size, eps_c);


    int max_gamma = get_max_gamma(alpha, K, eps_c);
    
    RK2_fms(alpha, K, nch_fn, eta, max_gamma, pop_size, p0, fileener, tl, r, tol);

    return 0;
}