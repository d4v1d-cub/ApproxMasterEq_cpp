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


typedef struct{
    int nfacn;      // number of factor nodes
    vector <long> fn_in; // list of factor nodes to which the nodes belongs
    vector <int> pos_fn;  // position in each factor node's list of nodes.
    long nch;   // the number of possible combinations of the states of 
                    // the neighboring clauses.
    long **fn_exc;  // remaining factor nodes after one removes a specific factor node
                    // first index: removed fn, second index: list of remaining fn.
    int **pos_fn_exc;   // position in the remaining fn in the same order as in fn_exc.
    int **fn_link_0;    // factor nodes where the node has a positive link
    int **fn_link_1;    // factor nodes where the node has a negative link
    int *count_l0;       // number of factor nodes in fn_link_0
    int *count_l1;       // number of factor nodes in fn_link_1
}Tnode;


typedef struct{
    int ch_unsat;  // the combination of the nodes that makes the clause unsatisfied.
    vector <long> nodes_in;  // nodes inside the factor node
    vector <int> links; // links to those nodes
    vector <int> pos_n;   // position in each node's list of factor nodes.
    long **nodes_exc;   // remaining nodes after one removes a specific node from the factor node.
}Thedge;


void init_graph(Tnode *&nodes, Thedge *&hedges, long N, long M){
    nodes = new Tnode[N];
    hedges = new Thedge[M];

    for (long i = 0; i < N; i++){
        nodes[i].nfacn = 0;
        nodes[i].fn_in = vector <long> ();
        nodes[i].pos_fn = vector <int> ();
    }

    for (long he = 0; he < M; he++){
        hedges[he].links = vector <int> ();
        hedges[he].nodes_in = vector <long> ();
        hedges[he].pos_n = vector <int> ();
    }
}


// This function reads all the information about the graph from a file.
void read_graph(char *filegraph, long N, long M, int K, 
                Tnode *&nodes, Thedge *&hedges){
    
    init_graph(nodes, hedges, N, M);
    string trash_str;
    double trash_double;

    vector <long> nodes_in;
    for (int j = 0; j < K; j++){
        nodes_in.push_back(0);
    }

    ifstream fg(filegraph);

    long fn_count = 0;
    bool new_fn;

    for (long i = 0; i < N; i++){
        fg >> trash_double;
        fg >> nodes[i].nfacn;
        nodes[i].nch = (long) pow(2, nodes[i].nfacn);
        getline(fg, trash_str);
        getline(fg, trash_str);
        nodes_in[0] = i;

        for (int k = 0; k < nodes[i].nfacn; k++){
            new_fn = true;
            for (int j = 0; j < K - 1; j++){
                fg >> trash_double;
                fg >> trash_double;
                fg >> nodes_in[j + 1];
                if (nodes_in[j + 1] < nodes_in[0]){
                    new_fn = false;
                }
                fg >> trash_double;
                fg >> trash_double;
            }
            // sort(nodes_in.begin(), nodes_in.end());
            if (new_fn){
                for (int w = 0; w < K; w++){
                    nodes[nodes_in[w]].fn_in.push_back(fn_count);
                    nodes[nodes_in[w]].pos_fn.push_back(w);
                    hedges[fn_count].nodes_in.push_back(nodes_in[w]);
                    hedges[fn_count].pos_n.push_back(nodes[nodes_in[w]].fn_in.size() - 1);
                }
                fn_count++;
            }
        }
    }

    for (long i = 0; i < N; i++){
        if (nodes[i].nfacn != nodes[i].fn_in.size()){
            //  cout << "Problem with node " << i << endl;
        }
    }

    fg.close();
}


long find_fn(vector <long> nodes_in, Thedge *hedges, vector <long> fn_list){
    bool cond = true;
    int w;
    int i = 0;
    while (i < fn_list.size() && cond){
        w = 0;
        while (cond && w < nodes_in.size()){
            if (nodes_in[w] != hedges[fn_list[i]].nodes_in[w]){
                cond = false;
            }
            w++;
        }
        i++;
        cond = !cond;
    }
    return fn_list[i - 1];
}


int find_node(vector <long> nodes_in, long node){
    bool cond = true;
    int i = 0;
    while (i < nodes_in.size() && cond){
        if (nodes_in[i] == node){
            cond = false;
        }
        i++;
    }
    return i - 1;
}


void read_graph_old_order(char *filegraph, long N, long M, int K, 
                          Tnode *&nodes, Thedge *&hedges){
    
    init_graph(nodes, hedges, N, M);
    string trash_str;
    double trash_double;

    vector <long> nodes_in;
    for (int j = 0; j < K; j++){
        nodes_in.push_back(0);
    }

    ifstream fg(filegraph);

    long fn_count = 0;
    long he;
    bool new_fn;
    long neigh;

    for (long i = 0; i < N; i++){
        fg >> trash_double;
        fg >> nodes[i].nfacn;
        nodes[i].nch = (long) pow(2, nodes[i].nfacn);
        getline(fg, trash_str);
        getline(fg, trash_str);

        for (int k = 0; k < nodes[i].nfacn; k++){
            nodes_in[0] = i;
            new_fn = true;
            for (int j = 0; j < K - 1; j++){
                fg >> trash_double;
                fg >> trash_double;
                fg >> nodes_in[j + 1];
                if (nodes_in[j + 1] < nodes_in[0]){
                    new_fn = false;
                    neigh = nodes_in[j + 1];
                }
                fg >> trash_double;
                fg >> trash_double;
            }
            sort(nodes_in.begin(), nodes_in.end());
            if (new_fn){
                nodes[i].fn_in.push_back(fn_count);
                hedges[fn_count].pos_n.push_back(nodes[i].fn_in.size() - 1);
                nodes[i].pos_fn.push_back(find_node(nodes_in, i));    
                for (int w = 0; w < K; w++){
                    
                    hedges[fn_count].nodes_in.push_back(nodes_in[w]);
                }
                fn_count++;
            }else{
                he = find_fn(nodes_in, hedges, nodes[neigh].fn_in);
                nodes[i].fn_in.push_back(he);
                nodes[i].pos_fn.push_back(find_node(nodes_in, i));
                hedges[he].pos_n.push_back(nodes[i].fn_in.size() - 1);
            }

        }
    }

    for (long i = 0; i < N; i++){
        if (nodes[i].nfacn != nodes[i].fn_in.size()){
            //  cout << "Problem with node " << i << endl;
        }
    }

    fg.close();
}



// This function read the links from a file
void read_links(char *filelinks, long N, long M, int K, Tnode *nodes, Thedge *hedges){
    ifstream fl(filelinks);
    int trash_int, link;

    for (long he = 0; he < M; he++){
        hedges[he].ch_unsat = 0;
        for (int w = 0; w < K; w++){
           hedges[he].links.push_back(0);
        }
    }

    for (long i = 0; i < N; i++){
        fl >> trash_int;
        for (int hind = 0; hind < nodes[i].nfacn; hind++){
            fl >> link;
            hedges[nodes[i].fn_in[hind]].links[nodes[i].pos_fn[hind]] = link;
            hedges[nodes[i].fn_in[hind]].ch_unsat += (((1 + link) / 2) << nodes[i].pos_fn[hind]);
        }
    }

    fl.close();
}

double av(Thedge *hedges, long M){
    double answ = 0;
    for (long i = 0; i < M; i++){
        answ += hedges[i].nodes_in.size();
    }
    return answ / M;
}

// This function creates at run time.
void create_graph(long N, long M, int K, Tnode *&nodes, Thedge *&hedges, gsl_rng * r){
    init_graph(nodes, hedges, N, M);
    int w, h;
    long var;
    bool cond;
    for (long he = 0; he < M; he++){
        hedges[he].ch_unsat = 0;
        w = 0;
        while (w < K){
            var = gsl_rng_uniform_int(r, N);
            cond = true;
            h = 0;
            while (h < hedges[he].nodes_in.size() && cond){
                if (hedges[he].nodes_in[h] == var){
                    cond = false;
                }
                h++;
            }

            if (cond){
                hedges[he].nodes_in.push_back(var);
                if (gsl_rng_uniform_pos(r) < 0.5){
                    hedges[he].links.push_back(1);
                    hedges[he].ch_unsat += (1 << w);
                }else{
                    hedges[he].links.push_back(-1);
                }
                hedges[he].pos_n.push_back(nodes[var].nfacn);
                nodes[var].fn_in.push_back(he);
                nodes[var].pos_fn.push_back(w);
                nodes[var].nfacn++;
                w++;
            }
        }
    }

    for (long i = 0; i < N; i++){
            nodes[i].nch = (long) pow(2, nodes[i].nfacn);
    }
}


void get_info_exc(Tnode *nodes, Thedge *hedges, long N, long M, int K){
    int w, count;
    for (long he = 0; he < M; he++){
        hedges[he].nodes_exc = new long *[K];
        for (int j = 0; j < K; j++){
            hedges[he].nodes_exc[j] = new long [K - 1];
            count = 0;
            w = (j + 1) % K;
            while (w != j){
                hedges[he].nodes_exc[j][count] = hedges[he].nodes_in[w];
                w = (w + 1) % K;
                count++;
            }
        } 
    }

    int other, count_l0, count_l1;
    for (long i = 0; i < N; i++){
        nodes[i].fn_exc = new long *[nodes[i].nfacn];
        nodes[i].pos_fn_exc = new int *[nodes[i].nfacn];
        nodes[i].fn_link_0 = new int *[nodes[i].nfacn];
        nodes[i].fn_link_1 = new int *[nodes[i].nfacn];
        nodes[i].count_l0 = new int [nodes[i].nfacn];
        nodes[i].count_l1 = new int [nodes[i].nfacn];
        for (int hind = 0; hind < nodes[i].nfacn; hind++){
            nodes[i].fn_exc[hind] = new long [nodes[i].nfacn - 1];
            nodes[i].pos_fn_exc[hind] = new int [nodes[i].nfacn - 1];
            other = (hind + 1) % nodes[i].nfacn;
            count = 0;
            count_l0 = 0;
            count_l1 = 0;
            while (other != hind){
                nodes[i].fn_exc[hind][count] = nodes[i].fn_in[other];
                nodes[i].pos_fn_exc[hind][count] = nodes[i].pos_fn[other];

                if (hedges[nodes[i].fn_in[other]].links[nodes[i].pos_fn[other]] == 1){
                    count_l0++;
                }else{
                    count_l1++;
                }
                count++;
                other = (other + 1) % nodes[i].nfacn;
            }

            nodes[i].count_l0[hind] = count_l0;
            nodes[i].count_l1[hind] = count_l1;
            nodes[i].fn_link_0[hind] = new int [count_l0];
            nodes[i].fn_link_1[hind] = new int [count_l1];
            
            other = (hind + 1) % nodes[i].nfacn;
            count_l0 = 0;
            count_l1 = 0;
            while (other != hind){
                if (hedges[nodes[i].fn_in[other]].links[nodes[i].pos_fn[other]] == 1){
                    nodes[i].fn_link_0[hind][count_l0] = other;
                    count_l0++;
                }else{
                    nodes[i].fn_link_1[hind][count_l1] = other;
                    count_l1++;
                }
                other = (other + 1) % nodes[i].nfacn;
            }
        }
    }
}


int get_max_c(Tnode *nodes, long N){
    int max_c = 0;
    for (long i = 0; i < N; i++){
        if (nodes[i].nfacn > max_c){
            max_c = nodes[i].nfacn;
        }
    }
    return max_c;
}


// initializes all the joint and conditional probabilities
void init_probs(double **&prob_joint, double ***&pu_cond, double **&pi, double **&me_sum, long M, int K, 
                int nch_fn, double p0){
    double prod;
    int bit;
    prob_joint = new double *[M];
    me_sum = new double *[M];
    pu_cond = new double **[M];
    for (long he = 0; he < M; he++){
        prob_joint[he] = new double [nch_fn];
        me_sum[he] = new double [nch_fn];
        for (int ch = 0; ch < nch_fn; ch++){
            prod = 1;
            for (int w = 0; w < K; w++){
                bit = ((ch >> w) & 1);
                prod *= (bit + (1 - 2 * bit) * p0);
            }
            prob_joint[he][ch] = prod;
        }

        pu_cond[he] = new double*[K];
        for (int w = 0; w < K; w++){
            pu_cond[he][w] = new double [2];
        }
    }

    pi = new double*[K];
    for (int w = 0; w < K; w++){
        pi[w] = new double[2];
    }
}


// initializes the auxiliary arrays for the Runge-Kutta integration
void init_RK_arr(double **&k1, double **&k2, double **&prob_joint_1, long M, 
                int nch_fn){
    k1 = new double *[M];
    k2 = new double *[M];
    prob_joint_1 = new double *[M];
    for (long he = 0; he < M; he++){
        k1[he] = new double [nch_fn];
        k2[he] = new double [nch_fn];
        prob_joint_1[he] = new double [nch_fn];
        for (int ch = 0; ch < nch_fn; ch++){
            k1[he][ch] = 0;
            k2[he][ch] = 0;
            prob_joint_1[he][ch] = 0;
        }
    }
}


// initializes the arrays where the binomial weights will be stored. Those arrays are used 
// to compute the probability of selecting the variable belonging to the smallest number 
// of satisfied clauses
void init_aux_arr(double *&poisson_probs, double *&poisson_sums, int max_c){
    poisson_probs = new double [max_c];
    poisson_sums = new double [max_c];
}


// it computes the binomial weights
void get_all_poisson_sums(int max_c, double pu_av, double *poisson_probs, 
                          double *poisson_sums, double mean_c){
    for (int sj = 0; sj < max_c; sj++){
        poisson_probs[sj] = gsl_ran_poisson_pdf(sj, (1 - pu_av) * mean_c);
    }
    
    for (int si = 0; si < max_c; si++){
        poisson_sums[si] = gsl_cdf_poisson_Q(si, (1 - pu_av) * mean_c);
    }
}



// it fills the array of the probabilities to be used when computing the walksat rate
void fill_pneigh(int S, double *poisson_probs, double *poisson_sums, 
                 double *pneigh){
    pneigh[0] = poisson_probs[S];
    pneigh[1] = poisson_sums[S];
}





// rate of the walksat algorithm used by Barthel et al. in 2003
// cj is a list of all the connectivities of the neighbors of node i that are 
// in unsatisfied clauses
// nch_exc is the number of possible combinations of the K-1 other variables in the clause
// therefore, nch_exc = 2^(K-1)
double rate_walksat(int E0, int S, int K, double q, double e_av, 
                    double *poisson_probs, double *poisson_sums, 
                    double *pneigh, int nch_exc){
    if (E0 > 0){
        bool cond;
        double cumul, prod;
        int cumul_bits, bit, k;
        cumul = 0;
    
        fill_pneigh(S, poisson_probs, poisson_sums, pneigh);
        for (int ch = 0; ch < nch_exc; ch++){
            prod = 1;
            cumul_bits = 0;
            for (int w = 0; w < K - 1; w++){
                bit = ((ch >> w) & 1);
                prod *= pneigh[bit];  
                cumul_bits += 1 - bit;
            }
            cumul += prod / (cumul_bits + 1);
        }
        return E0 * (q / K + (1 - q) * cumul) / e_av; 
    }else{
        return 0;
    }
}


// it computes the conditional probabilities of having a partially unsatisfied clause, given the 
// value of one variable in the clause
void comp_pcond(double **prob_joint, double ***pu_cond, double **pi, Thedge *hedges, long M, int K, 
                int nch_fn){
    double pu;
    int bit;
    int ch_uns_flip;
    for (long he = 0; he < M; he++){
        for (int w = 0; w < K; w++){
            for (int s = 0; s < 2; s++){
                pi[w][s] = 0;
            }
        }

        for (int ch = 0; ch < nch_fn; ch++){
            for (int w = 0; w < K; w++){
                bit = ((ch >> w) & 1);
                pi[w][bit] += prob_joint[he][ch];
            }
        }

        for (int w = 0; w < K; w++){
            bit = ((hedges[he].ch_unsat >> w) & 1); 
            ch_uns_flip = (hedges[he].ch_unsat ^ (1 << w));
            pu_cond[he][w][bit] = prob_joint[he][hedges[he].ch_unsat] / pi[w][bit];
            pu_cond[he][w][1 - bit] = prob_joint[he][ch_uns_flip] / pi[w][1 - bit];
        }
    }
}


// it gets the conditional probabilities for the factor nodes unsatisfied by
// si=1 (pu_l[0]) and the ones unsatisfied by si=-1 (pu_l[1])
// the second index is the value of the spin in the conditional
void get_pu_l(double ***pu_cond, double ***pu_l, int fn_src, Tnode node){
    int index;
    for (int i = 0; i < node.count_l1[fn_src]; i++){
        index = node.fn_link_1[fn_src][i];
        pu_l[0][0][i] = pu_cond[node.fn_in[index]][node.pos_fn[index]][0];
        pu_l[0][1][i] = pu_cond[node.fn_in[index]][node.pos_fn[index]][1];
        // The links whose value is -1 (fn_link_1) are the ones unsatisfied by the
        // node if it has the value 1 (index 0 in the arrays)
    }
    for (int i = 0; i < node.count_l0[fn_src]; i++){
        index = node.fn_link_0[fn_src][i];
        pu_l[1][0][i] = pu_cond[node.fn_in[index]][node.pos_fn[index]][0];
        pu_l[1][1][i] = pu_cond[node.fn_in[index]][node.pos_fn[index]][1];
        // The links whose value is -1 (fn_link_1) are the ones unsatisfied by the
        // node if it has the value 1 (index 0 in the arrays)
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


void init_aux_arr(double ***&pu_l, double ***&fE, double *&fEnew, int c){
    fEnew = new double [c];

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
            pu_l[s][si] = new double [c];
            fE[s][si] = new double [c];
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
void sum_walksat(long node, int fn_src, Tnode *nodes, Thedge *hedges, 
             double *prob_joint, double ***pu_cond, double *poisson_probs, double *poisson_sums, 
             int nch_fn, double e_av, double *me_sum_src, int K, double q){

    double ***pu_l, ***fE, *fEnew;
    init_aux_arr(pu_l, fE, fEnew, nodes[node].nfacn);

    double *pneigh;
    pneigh = new double [2];
    
    get_pu_l(pu_cond, pu_l, fn_src, nodes[node]);
    // remember that when l=1 the unsatisfying assingment is si=-1
    // therefore, count_l1 corresponds to pu_l[0], and count_l0 to pu_l[1]
    for (int s1 = 0; s1 < 2; s1++){
        for (int s2 = 0; s2 < 2; s2++){
            fE[s1][s2][0] = 1;
        }
    }
    recursive_marginal(pu_l[0][0], nodes[node].count_l1[fn_src], 0, fE[0][0], fEnew);
    recursive_marginal(pu_l[0][1], nodes[node].count_l1[fn_src], 0, fE[0][1], fEnew);
    recursive_marginal(pu_l[1][0], nodes[node].count_l0[fn_src], 0, fE[1][0], fEnew);
    recursive_marginal(pu_l[1][1], nodes[node].count_l0[fn_src], 0, fE[1][1], fEnew);

    double terms[2][2];
    int E[2];

    long he;
    int plc_he, ch_flip;
    bool bit, uns, uns_flip;

    for (E[0] = 0; E[0] < nodes[node].count_l1[fn_src] + 1; E[0]++){
        for (E[1] = 0; E[1] < nodes[node].count_l0[fn_src] + 1; E[1]++){

            terms[0][0] = rate_walksat(E[0], nodes[node].nfacn - E[0], K, q, e_av, poisson_probs, poisson_sums, pneigh, nch_fn / 2) * 
                          fE[0][0][E[0]] * fE[1][0][E[1]];
            terms[1][0] = rate_walksat(E[1], nodes[node].nfacn - E[1], K, q, e_av, poisson_probs, poisson_sums, pneigh, nch_fn / 2) * 
                          fE[0][1][E[0]] * fE[1][1][E[1]];

            he = nodes[node].fn_in[fn_src];
            plc_he = nodes[node].pos_fn[fn_src];
            bit = ((hedges[he].ch_unsat >> plc_he) & 1);
            
            terms[bit][1] = rate_walksat(E[bit] + 1, nodes[node].nfacn - E[bit] - 1, K, q, e_av, poisson_probs, poisson_sums, pneigh, nch_fn / 2) * 
                            fE[bit][bit][E[bit]] * fE[1 - bit][bit][E[1 - bit]];
            terms[1 - bit][1] = rate_walksat(E[1 - bit], nodes[node].nfacn - E[1 - bit], K, q, e_av, poisson_probs, poisson_sums, pneigh, nch_fn / 2) * 
                                fE[1 - bit][1 - bit][E[1 - bit]] * fE[bit][1 - bit][E[bit]];
            
            for (int ch_src = 0; ch_src < nch_fn; ch_src++){
                bit = ((ch_src >> plc_he) & 1);
                ch_flip = (ch_src ^ (1 << plc_he));
                uns = (ch_src == hedges[he].ch_unsat);
                uns_flip = (ch_flip == hedges[he].ch_unsat);
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
    delete [] pneigh;
    pneigh = NULL;
}


// it computes all the derivatives of the joint probabilities
void der_walksat(Tnode *nodes, Thedge *hedges, double **prob_joint, double ***pu_cond, 
             double *poisson_probs, double *poisson_sums, long M, int K, int nch_fn, double e_av, 
             double **me_sum, double q){
    for (long he = 0; he < M; he++){
        for (int ch = 0; ch < nch_fn; ch++){
            me_sum[he][ch] = 0;
        }
    }

    // candidate to be a parallel for
    #pragma omp parallel for
    for (long he = 0; he < M; he++){
        for (int w = 0; w < K; w++){
            sum_walksat(hedges[he].nodes_in[w], hedges[he].pos_n[w], nodes, hedges, 
                    prob_joint[he], pu_cond, poisson_probs, poisson_sums, nch_fn, e_av, me_sum[he],
                    K, q);
        }
    }
}


double energy(double **prob_joint, Thedge *hedges, long M){
    double e = 0;
    for (long he = 0; he < M; he++){
        e += prob_joint[he][hedges[he].ch_unsat];
    }
    return e;
}


// peforms the integration of the differential equations with the 2nd order Runge-Kutta
// the method is implemented with adaptive step size
void RK2_walksat(Tnode *nodes, Thedge *hedges, long N, long M, int K, int nch_fn, 
                 double q, int max_c, double p0, char *fileener, double tl, 
                 double tol = 1e-2, double t0 = 0, double dt0 = 0.01, 
                 double ef = 1e-6, double dt_min = 1e-7){
    double *poisson_probs, *poisson_sums;
    double **prob_joint, ***pu_cond, **me_sum, **pi;
    double e, pu_av, error;                 
    
    init_aux_arr(poisson_probs, poisson_sums, max_c);
    init_probs(prob_joint, pu_cond, pi, me_sum, M, K, nch_fn, p0);
    double mean_c = double(K * M) / N;

    // initialize auxiliary arrays for the Runge-Kutta integration
    double **k1, **k2, **prob_joint_1;
    init_RK_arr(k1, k2, prob_joint_1, M, nch_fn);

    ofstream fe(fileener);
    
    e = energy(prob_joint, hedges, M);
    pu_av = e / M;
    fe << t0 << "\t" << e / N << endl;   // it prints the energy density

    double dt1 = dt0;
    double t = t0;

    bool valid;

    // the time scale is already given in Monte Carlo steps. Inside the rates I am using 
    // the energy density e_av
    while (t < tl){
        if (e / N < ef){
            //  cout << "Final energy reached" << endl;
            break;
        }

        auto t1 = std::chrono::high_resolution_clock::now();

        comp_pcond(prob_joint, pu_cond, pi, hedges, M, K, nch_fn);
        get_all_poisson_sums(max_c, pu_av, poisson_probs, poisson_sums, mean_c);

        der_walksat(nodes, hedges, prob_joint, pu_cond, poisson_probs, poisson_sums, M, K, nch_fn, 
                     e / N, me_sum, q);   // in the rates, I use the energy density

        valid = true;
        for (long he = 0; he < M; he++){
            for (int ch = 0; ch < nch_fn; ch++){
                k1[he][ch] = dt1 * me_sum[he][ch];
                prob_joint_1[he][ch] = prob_joint[he][ch] + k1[he][ch];
                if (prob_joint_1[he][ch] < 0){
                    valid = false;
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
            for (long he = 0; he < M; he++){
                for (int ch = 0; ch < nch_fn; ch++){
                    k1[he][ch] = dt1 * me_sum[he][ch];
                    prob_joint_1[he][ch] = prob_joint[he][ch] + k1[he][ch];
                    if (prob_joint_1[he][ch] < 0){
                        valid = false;
                    }
                }
            }
        }
        
        e = energy(prob_joint_1, hedges, M);
        pu_av = e / M;
        comp_pcond(prob_joint_1, pu_cond, pi, hedges, M, K, nch_fn);
        get_all_poisson_sums(max_c, pu_av, poisson_probs, poisson_sums, mean_c);

        der_walksat(nodes, hedges, prob_joint_1, pu_cond, poisson_probs, poisson_sums, M, K, 
                    nch_fn, e / N, me_sum, q);
            
        valid = true;
        for (long he = 0; he < M; he++){
            for (int ch = 0; ch < nch_fn; ch++){
                k2[he][ch] = dt1 * me_sum[he][ch];
                if (prob_joint[he][ch] + (k1[he][ch] + k2[he][ch]) / 2 < 0){
                    valid = false;
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
            e = energy(prob_joint, hedges, M);
            pu_av = e / M;
        }else{
            error = 0;
            for (long he = 0; he < M; he++){
                for (int ch = 0; ch < nch_fn; ch++){
                    error += fabs(k1[he][ch] - k2[he][ch]);
                }
            }

            error /= nch_fn * M;

            if (error < 2 * tol){
                //  cout << "step dt=" << dt1 << "  accepted" << endl;
                //  cout << "error=" << error << endl;
                t += dt1;
                for (long he = 0; he < M; he++){
                    for (int ch = 0; ch < nch_fn; ch++){
                        prob_joint[he][ch] += (k1[he][ch] + k2[he][ch]) / 2;
                    }
                }
                e = energy(prob_joint, hedges, M);
                pu_av = e / M;
                fe << t << "\t" << e / N << endl;

            }else{
                e = energy(prob_joint, hedges, M);
                pu_av = e / M;
                //  cout << "step dt=" << dt1 << "  rejected  new step will be attempted" << endl;
                //  cout << "error=" <<  error << endl;
            }

            dt1 = 4 * dt1 * sqrt(2 * tol / error) / 5;
            if (dt1 > M){
                    dt1 = M;
            }else if(dt1 < dt_min){
                    dt1 = dt_min;
            }

            //  cout << "Recommended step is dt=" << dt1 << endl;
        }

        auto t2 = std::chrono::high_resolution_clock::now();

        auto ms_int = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);

        // cout << "iteration time:   " << ms_int.count() << "ms" << endl; 
    }

    fe.close();

}


int main(int argc, char *argv[]) {
    long N = atol(argv[1]);
    long M = atol(argv[2]);
    int K = atoi(argv[3]);
    unsigned long seed_r = atol(argv[4]);
    double q = atof(argv[5]);
    double tl = atof(argv[6]);
    double tol = atof(argv[7]);
    int nthr = atoi(argv[8]);

    int nch_fn = (1 << K);
    double p0 = 0.5;

    omp_set_num_threads(nthr);

    Tnode *nodes;
    Thedge *hedges;

    gsl_rng * r;
    init_ran(r, seed_r);

    // char filegraph[300];
    // char filelinks[300];
    // sprintf(filegraph, "KSATgraph_K_%d_N_%li_M_%li_simetric_1_model_1_idum1_-2_J_1_ordered.txt", 
    //                    K, N, M);
    // sprintf(filelinks, "KSAT_K_%d_enlaces_N_%li_M_%li_idumenlaces_-2_idumgraph_-2_ordered.txt", 
    //                    K, N, M);

    char fileener[300]; 
    sprintf(fileener, "CDA_WalkSAT_av_rates_ener_K_%d_N_%li_M_%li_q_%.4lf_tl_%.2lf_seed_%li_tol_%.1e.txt", 
            K, N, M, q, tl, seed_r, tol);

    create_graph(N, M, K, nodes, hedges, r);
    // read_graph_old_order(filegraph, N, M, K, nodes, hedges);
    // read_links(filelinks, N, M, K, nodes, hedges);
    int max_c = get_max_c(nodes, N);
    get_info_exc(nodes, hedges, N, M, K);

    
    RK2_walksat(nodes, hedges, N, M, K, nch_fn, q, max_c, p0, fileener, tl, tol);

    return 0;
}