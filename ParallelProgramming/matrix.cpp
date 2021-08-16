#include <iostream>
#include <vector>
#include <thread>
#include <fstream>

class Timer {
public:
    Timer() {
        begin = std::chrono::steady_clock::now();
    }

    ~Timer() {
        auto end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
        std::cout << elapsed_ms.count() << " ms" << std::endl;
    }

private:
    std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long int, std::ratio<1, 1000000000>>>
            begin;


};

constexpr int block_size = 8;

class Matrix {
private:
    static size_t thread_cnt_;

    static size_t get_thread_cnt() { return thread_cnt_; }

    struct BlockMatr {
    private:

        struct alignas(64) Block {// sizeof = 28

            Block() = default;

            ~Block() = default;

            std::vector<std::vector<int>> matr =
                    std::vector<std::vector<int>>(block_size,
                                                  std::vector<int>(block_size, 0));//string will fit in line

            char padding[36];

            friend Block operator*(const Block& first, const Block& second) {

                Block result;
                for (int i = 0; i < block_size; ++i) {
                    for (int k = 0; k < block_size; ++k) {
                        int r = first.matr[i][k];
                        for (int j = 0; j < block_size; ++j) {
                            result.matr[i][j] += r * second.matr[k][j];
                        }
                    }
                }

                /*
                Block transposed_second = second.get_transposed();

                for (int i = 0; i < block_size; ++i) {
                    for (int j = 0; j < block_size; ++j) {
                        for (int k = 0; k < block_size; ++k) {
                            result.matr[i][j] += first.matr[i][k] * transposed_second.matr[j][k];
                        }
                    }
                }*/
                return result;
            }

            Block& operator+=(const Block& other) {
                for (int i = 0; i < block_size; ++i) {
                    for (int j = 0; j < block_size; ++j) {
                        matr[i][j] += other.matr[i][j];
                    }
                }
                return *this;
            }

            Block get_transposed() const {
                Block result;
                for (int i = 0; i < block_size; ++i) {
                    for (int j = 0; j < block_size; ++j) {
                        result.matr[i][j] = matr[j][i];
                    }
                }
                return result;
            }
        };

        int height_;
        int width_;
        int block_height_ = height_ / block_size;
        int block_width_ = width_ / block_size;

        std::vector<std::vector<Block>> block_matr_;


        std::vector<std::pair<int, int>> prepare_job() {
            int total = block_height_ * block_width_;
            int cells_per_thread = total / Matrix::thread_cnt_;
            std::vector<int> distribution(Matrix::thread_cnt_, cells_per_thread);


            for (int i = 0, rest = total % Matrix::thread_cnt_; rest > 0; ++i, --rest) {
                ++distribution[i];
            }

            std::vector<std::pair<int, int>> threads_work(Matrix::thread_cnt_);
            for (int i = 0, current = 0; i < Matrix::thread_cnt_; ++i) {
                threads_work[i].first = current;
                current = current + distribution[i] - 1;
                threads_work[i].second = current;
                ++current;
            }
            return threads_work;
        }

    public:

        int& get_by_index(const int i, const int j) {
            return block_matr_[i / block_size][j / block_size].matr[i % block_size][j % block_size];
        }

        const int& get_by_index(const int i, const int j) const {
            return block_matr_[i / block_size][j / block_size].matr[i % block_size][j % block_size];
        }

        BlockMatr(const Matrix& matrix) :
                height_(matrix.height_),
                width_(matrix.width_),
                block_height_((height_ % block_size) ? height_ / block_size + 1 : height_ / block_size),
                block_width_((width_ % block_size) ? width_ / block_size + 1 : width_ / block_size),
                block_matr_(std::vector<std::vector<Block>>(block_height_, std::vector<Block>(block_width_))) {



            for (int i = 0; i < matrix.height_; ++i) {
                for (int j = 0; j < matrix.width_; ++j) {
                    this->get_by_index(i, j) = matrix.matr_[i][j];
                }
            }
        }

        BlockMatr(const int height, const int width) :
                height_(height),
                width_(width),
                block_height_((height_ % block_size) ? height_ / block_size + 1 : height_ / block_size),
                block_width_((width_ % block_size) ? width_ / block_size + 1 : width_ / block_size),
                block_matr_(std::vector<std::vector<Block>>(block_height_, std::vector<Block>(block_width_))) {}


        friend BlockMatr operator*(const BlockMatr& first, const BlockMatr& second) {
            Matrix::BlockMatr result(first.height_, second.width_);

            auto threads_work = result.prepare_job();

            auto job = [&](const int first_elem, const int last_elem) {
                for (int cnt = first_elem; cnt <= last_elem; ++cnt) {
                    int i = cnt % result.block_height_;
                    int j = cnt / result.block_width_;

                    for (int k = 0; k < first.block_width_; ++k) {
                        result.block_matr_[i][j] += (first.block_matr_[i][k] * second.block_matr_[k][j]);
                    }
                }
            };

            if (Matrix::thread_cnt_ > 1) {

                std::vector<std::thread> workers(Matrix::thread_cnt_ - 1);

                for (int i = 0; i < Matrix::thread_cnt_ - 1; ++i) {
                    workers[i] = std::thread(job, threads_work[i].first, threads_work[i].second);
                }

                job(threads_work.back().first, threads_work.back().second);

                for (auto& worker : workers) {
                    if (worker.joinable()) {
                        worker.join();
                    }
                }

            } else {
                job(threads_work.back().first, threads_work.back().second);
            }


            return result;
        }
    };

    void copy_from_block_matr(const BlockMatr& block_matrix) {
        for (int i = 0; i < height_; ++i) {
            for (int j = 0; j < width_; ++j) {
                matr_[i][j] = block_matrix.get_by_index(i, j);
            }
        }
    }

    friend BlockMatr operator*(const BlockMatr& first, const BlockMatr& second);

public:

    const int height_ = 0;
    const int width_ = 0;
    std::vector<std::vector<int>> matr_;

    Matrix(const int height, const int width) :
            height_(height),
            width_(width),
            matr_(std::vector<std::vector<int>>(height_, std::vector<int>(width_, 0))) {}

    static void set_thread_cnt(const size_t thread_cnt) noexcept { thread_cnt_ = thread_cnt; }

    friend Matrix operator*(const Matrix& first, const Matrix& second);

    void print() {
        for (int i = 0; i < height_; ++i) {
            for (int j = 0; j < width_; ++j) {
                std::cout << matr_[i][j] << ' ';
            }
            std::cout << std::endl;
        }
    }

    void load(const std::string& path) {
        std::ifstream fin(path);
        if (fin.fail()) {
            exit(1);
        }
        for (int cnt = 0, cells_cnt = height_ * width_; cnt < cells_cnt; ++cnt) {
            fin >> matr_[cnt / height_][cnt % width_];
        }
    }

    bool operator==(const Matrix& other) {
        for (int i = 0; i < height_; ++i) {
            for (int j = 0; j < width_; ++j) {
                if (matr_[i][j] != other.matr_[i][j]) {
                    return false;
                }
            }
        }
        return true;
    }

};

size_t Matrix::thread_cnt_;

Matrix operator*(const Matrix& first, const Matrix& second) {

    Matrix::BlockMatr bm_first(first);
    Matrix::BlockMatr bm_second(second);

    Matrix::BlockMatr bm_result = bm_first * bm_second;

    Matrix result(first.height_, second.width_);
    result.copy_from_block_matr(bm_result);

    return result;
}


int main() {
    Matrix::set_thread_cnt(5);
    Matrix m1(1'000, 1'000);
    Matrix m2(1'000, 1'000);
//    Matrix ans(100, 100);

    std::string first("/home/artemiy/CLionProjects/Matrix/first.txt");
    std::string second("/home/artemiy/CLionProjects/Matrix/second.txt");
//    std::string result("/home/artemiy/CLionProjects/Matrix/ans.txt");
    m1.load(first);
    m2.load(second);
//    ans.load(result);


    Timer* t = new Timer;
    Matrix m3 = m1 * m2;
    delete t;
//    std::cout << std::boolalpha << (m3 == ans) << std::endl;


//    m3.print();


    return 0;
}
