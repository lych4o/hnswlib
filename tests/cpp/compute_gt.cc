#include <iostream>
#include <fstream>
#include <queue>
#include <chrono>
#include "../../hnswlib/hnswlib.h"


#include <unordered_set>

using namespace hnswlib;

int main() {
    int subset_size_milllions = 100;
    int efConstruction = 40;
    int M = 24;

    size_t vecsize = subset_size_milllions * 1000000;

    size_t qsize = 10000;

    size_t vecdim = 128;
    char path_gt[1024];
    const char *path_q = "/home/ycli/data/query.public.10K.u8bin";
    const char *path_data = "/home/ycli/data/learn.100M.u8bin";
    // const char *path_data = "/root/xyzhi/data/sift/10M.u8bin";

    snprintf(path_gt, sizeof(path_gt), "/home/ycli/data/compute_gt/bigann-%dM", subset_size_milllions);

    
    std::cout << "Create GT file: "<<path_gt<<"\n";
    std::ofstream outputGT(path_gt, std::ios::binary);
    if (!outputGT.is_open()) {
        std::cerr << "Error opening ground truth file: " << path_gt << std::endl;
        return -1;
    }
    unsigned int *massQA = new unsigned int[qsize * 1000];
    uint32_t gt_num = qsize, gt_nn = 100;
    outputGT.write((char *) &gt_num, sizeof(uint32_t));
    outputGT.write((char *) &gt_nn, sizeof(uint32_t));
    std::cout<< "gt_num " << gt_num <<" gt_nn " << gt_nn<< std::endl;
    

    std::cout << "Loading queries:\n";
    unsigned char *massb = new unsigned char[vecdim];
    unsigned char *massQ = new unsigned char[qsize * vecdim];
    std::ifstream inputQ(path_q, std::ios::binary);
    uint32_t q_num, q_dim;
    inputQ.read((char *) &q_num, sizeof(uint32_t));
    inputQ.read((char *) &q_dim, sizeof(uint32_t));
    std::cout<< "q_num " << q_num <<" q_dim " << q_dim<< std::endl;
    for (int i = 0; i < qsize; i++) {
        inputQ.read((char *) massb, q_dim);
        for (int j = 0; j < vecdim; j++) {
            massQ[i * vecdim + j] = massb[j];
        }
    }
    inputQ.close();


    unsigned char *mass = new unsigned char[vecdim];
    std::ifstream input(path_data, std::ios::binary);
    int in = 0;
    L2SpaceI l2space(vecdim);

    HierarchicalNSW<int> *appr_alg;
    
    std::cout << "Compute gt ...\n";
    appr_alg = new HierarchicalNSW<int>(&l2space, vecsize, M, efConstruction);
    uint32_t base_num;
    input.read((char *) &base_num, 4);
    input.read((char *) &in, 4);
    if (in != 128) {
        std::cout << "file error";
        exit(1);
    }
    std::cout<<"base_num "<< base_num << " base_dim " << in <<std::endl;

    char* base_vec = (char*) malloc((size_t)base_num*in);
    input.read(base_vec, (size_t)base_num*in);

#ifdef DEBUG
    qsize = 1;
#endif

#pragma omp parallel for
    for (int i = 0; i < qsize; i++) {
        std::priority_queue<std::pair<int, labeltype >> gt;
        #ifdef DEBUG
        vecsize = 110;
        #endif
        for(int j = 0;j<vecsize;j++){
            int dist = appr_alg->compute_single_onbase(massQ + vecdim * i, base_vec, in, j);
            #ifdef DEBUG
                std::cout<<j <<"-"<<dist<<" ";
            #endif
            if (gt.size() < 100) {
                gt.push(std::make_pair(dist, j));
            } else if (dist < gt.top().first) {
                gt.pop();
                gt.push(std::make_pair(dist, j));
            }
        }
        #ifdef DEBUG
            std::cout<<"\n";
        #endif

        // Extract the nearest neighbors in order
        std::vector<labeltype> neighbors;
        #ifdef DEBUG
            std::cout<<"gtt: \n";
        #endif
        while (!gt.empty()) {
            #ifdef DEBUG
                std::cout<<gt.top().second << " ";
            #endif
            neighbors.push_back(gt.top().second);
            gt.pop();
        }
        #ifdef DEBUG
            std::cout<<"\n";
        #endif

        // Store the results
        #pragma omp critical
        {
            if(i%100 ==0){
                printf("%.2f%% ", i*100.0/qsize);
            }
            for (int k = 0; k < 100; k++) {
                massQA[i * 100 + k] = neighbors[100-k-1];
            }
        }
    }
    // Write ground truth results
    #ifndef DEBUG
    for (int i = 0; i < qsize; i++) {
        outputGT.write((char *)(massQA + i * 100), 100 * sizeof(unsigned int));
    }
    std::cout << "Write gt over...\n";
    #endif

    input.close();
    #ifndef DEBUG
    outputGT.close();
    #endif
    
    return 0;
}