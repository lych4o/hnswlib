#include <iostream>
#include <fstream>
#include <queue>
#include <chrono>
#include "../../hnswlib/hnswlib.h"


#include <unordered_set>

using namespace std;
using namespace hnswlib;

class StopW {
    std::chrono::steady_clock::time_point time_begin;
 public:
    StopW() {
        time_begin = std::chrono::steady_clock::now();
    }

    float getElapsedTimeMicro() {
        std::chrono::steady_clock::time_point time_end = std::chrono::steady_clock::now();
        return (std::chrono::duration_cast<std::chrono::microseconds>(time_end - time_begin).count());
    }

    void reset() {
        time_begin = std::chrono::steady_clock::now();
    }
};



/*
* Author:  David Robert Nadeau
* Site:    http://NadeauSoftware.com/
* License: Creative Commons Attribution 3.0 Unported License
*          http://creativecommons.org/licenses/by/3.0/deed.en_US
*/

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))

#include <unistd.h>
#include <sys/resource.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
#include <fcntl.h>
#include <procfs.h>

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)

#endif

#else
#error "Cannot define getPeakRSS( ) or getCurrentRSS( ) for an unknown OS."
#endif


/**
* Returns the peak (maximum so far) resident set size (physical
* memory use) measured in bytes, or zero if the value cannot be
* determined on this OS.
*/
static size_t getPeakRSS() {
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    return (size_t)info.PeakWorkingSetSize;

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
    /* AIX and Solaris ------------------------------------------ */
    struct psinfo psinfo;
    int fd = -1;
    if ((fd = open("/proc/self/psinfo", O_RDONLY)) == -1)
        return (size_t)0L;      /* Can't open? */
    if (read(fd, &psinfo, sizeof(psinfo)) != sizeof(psinfo)) {
        close(fd);
        return (size_t)0L;      /* Can't read? */
    }
    close(fd);
    return (size_t)(psinfo.pr_rssize * 1024L);

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    /* BSD, Linux, and OSX -------------------------------------- */
    struct rusage rusage;
    getrusage(RUSAGE_SELF, &rusage);
#if defined(__APPLE__) && defined(__MACH__)
    return (size_t)rusage.ru_maxrss;
#else
    return (size_t) (rusage.ru_maxrss * 1024L);
#endif

#else
    /* Unknown OS ----------------------------------------------- */
    return (size_t)0L;          /* Unsupported. */
#endif
}


/**
* Returns the current resident set size (physical memory use) measured
* in bytes, or zero if the value cannot be determined on this OS.
*/
static size_t getCurrentRSS() {
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    return (size_t)info.WorkingSetSize;

#elif defined(__APPLE__) && defined(__MACH__)
    /* OSX ------------------------------------------------------ */
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
        (task_info_t)&info, &infoCount) != KERN_SUCCESS)
        return (size_t)0L;      /* Can't access? */
    return (size_t)info.resident_size;

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
    /* Linux ---------------------------------------------------- */
    long rss = 0L;
    FILE *fp = NULL;
    if ((fp = fopen("/proc/self/statm", "r")) == NULL)
        return (size_t) 0L;      /* Can't open? */
    if (fscanf(fp, "%*s%ld", &rss) != 1) {
        fclose(fp);
        return (size_t) 0L;      /* Can't read? */
    }
    fclose(fp);
    return (size_t) rss * (size_t) sysconf(_SC_PAGESIZE);

#else
    /* AIX, BSD, Solaris, and Unknown OS ------------------------ */
    return (size_t)0L;          /* Unsupported. */
#endif
}

inline bool exists_test(const std::string &name) {
    ifstream f(name.c_str());
    return f.good();
}

void read_gt(const std::string &path, std::vector<std::unordered_map<labeltype,int>> &answers, 
        uint32_t qsize, uint32_t k) {
    ifstream input_data(path, ios::binary);
    if (!input_data.is_open()) {
        std::cout << "Error opening GT file: " << path << endl;
        exit(-1);
    }

    int qsize_input, k_input;
    input_data.read((char *) &qsize_input, sizeof(int));
    input_data.read((char *) &k_input, sizeof(int));

    if (qsize != qsize_input || k != k_input) {
        std::cout << "Error: expected " << qsize << " queries with " << k 
             << " neighbors, but got " << qsize_input << " queries with " 
             << k_input << " neighbors" << endl;
        exit(-1);
    }

    answers.clear();
    answers.resize(qsize);

    unsigned int *id_buffer = new unsigned int[qsize * k];
    float *dis_buffer = new float[qsize * k];

    input_data.read((char *) id_buffer, qsize * k * sizeof(unsigned int));
    input_data.read((char *) dis_buffer, qsize * k * sizeof(float));

    for (uint32_t i = 0; i < qsize; i++) {
        for (uint32_t j = 0; j < k; j++) {
            labeltype label = id_buffer[i * k + j];
            int dis = static_cast<int>(dis_buffer[i * k + j] + 0.5);
            answers[i][label] = dis;
        }
    }

    delete[] id_buffer;
    delete[] dis_buffer;
}

unsigned char* read_bin(const std::string &path, uint32_t row_size, uint32_t column_size, uint32_t item_size) {
    uint32_t row_size_input, column_size_input;
    ifstream input_data(path, ios::binary);
    if (!input_data.is_open()) {
        cout << "Error opening data file: " << path << endl;
        exit(-1);
    }
    input_data.read((char *) &row_size_input, sizeof(uint32_t));
    input_data.read((char *) &column_size_input, sizeof(uint32_t));
    
    if (row_size != row_size_input || column_size != column_size_input) {
        cout << "Error: expected " << row_size << " points of dimension " << column_size 
             << ", but got " << row_size_input << " points of dimension " << column_size_input << endl;
        exit(-1);
    }

    unsigned char *data = new unsigned char[1ull * row_size * column_size * item_size];
    input_data.read((char *) data, 1ull * row_size * column_size * item_size);

    return data;
}

void sift_test100M() {
    int efc = 512;
    int M = 32;

    std::string base_data_path = "/home/ycli/data/learn.100M.u8bin";
    std::string query_data_path = "/home/ycli/data/query.public.10K.u8bin";
    std::string gt_path = "/home/ycli/data/compute_gt/bigann-100M";
    std::string index_path = "sift100M_ef_" + std::to_string(efc) +"_M_" + 
        std::to_string(M) + ".bin";

    uint32_t num = 100000000;
    uint32_t dim = 128;
    uint32_t qsize = 10000;
    uint32_t k = 100;


    L2SpaceI l2space(dim);

    HierarchicalNSW<int> *appr_alg;
    if (exists_test(index_path)) {
        cout << "Loading index from " << index_path << ":\n";
        appr_alg = new HierarchicalNSW<int>(&l2space, index_path, false);
        cout << "Actual memory usage: " << getCurrentRSS() / 1000001 << "Mb \n";
    } else {
        cout << "Building index:\n";
        appr_alg = new HierarchicalNSW<int>(&l2space, num, M, efc);

        unsigned char *data = read_bin(base_data_path, num, dim, sizeof(uint8_t));
        cout << "Read binary file done\n";
        std::atomic<size_t> num_add(0);
        #pragma omp parallel for
        for (size_t i = 0; i < num; i++) {
            appr_alg->addPoint((void*) (data + dim * i), i);
            size_t current_num = num_add.fetch_add(1);
            if (current_num % 10000 == 0) {
                cout << "Added point " << current_num << " / " << num << "\n";
            }
        }
        appr_alg->saveIndex(index_path);
        cout << "Index saved to " << index_path << "\n";
        delete[] data;
    }

    fflush(stdout);
    cout << "Loading queries:\n";
    unsigned char *query_data = read_bin(query_data_path, qsize, dim, sizeof(uint8_t));

    cout << "Loading GT:\n";
    std::vector<std::unordered_map<labeltype, int>> answers;
    read_gt(gt_path, answers, qsize, k);

    // Prepare efs
    // vector<size_t> efs = {100};
    vector<size_t> efs;
    for (size_t i = 100; i < 500; i += 50) {
        efs.emplace_back(i);
    }
    
    for (size_t ef : efs) {
        appr_alg->setEf(ef);
        appr_alg->metric_distance_computations = 0;
        StopW stopw = StopW();

        size_t correct = 0;
        size_t total = 0;

        for (size_t i = 0; i < qsize; i++) {
            // printf("before search knn\n");
            std::priority_queue<std::pair<int, labeltype>> result =
                appr_alg->searchKnn(query_data + dim * i, k);

            total += k;
            /*
            printf("query %zu answers: ", i);
            std::priority_queue<std::pair<int, labeltype>> answers_queue;
            for (const auto& answer: answers[i]) {
                answers_queue.emplace(answer.second, answer.first);
            }
            while(answers_queue.size()) {
                printf("(%zu, %d) ", answers_queue.top().second, answers_queue.top().first);
                answers_queue.pop();
            }
            printf("\nresults: ");
            */

            while (!result.empty()) {
                // printf("(%zu, %d) ", result.top().second, result.top().first);
                if (answers[i].find(result.top().second) != answers[i].end()) {
                    correct++;
                }
                result.pop();
            }
            // printf("\n");
        }

        float recall = static_cast<float>(correct) / total;
        float time_us_per_query = stopw.getElapsedTimeMicro() / qsize;

        cout << ef << "\t" << recall << "\t" << time_us_per_query << " us\t"
             << appr_alg->metric_distance_computations << "\n";
    }

    cout << "Actual memory usage: " << getCurrentRSS() / 1000000 << " Mb \n";
    delete[] query_data;

    return ;
}
